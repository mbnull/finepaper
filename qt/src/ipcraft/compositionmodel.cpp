#include "ipcraft/compositionmodel.h"

#include "ipcraft/jsonhelpers.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {

void insertString(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

void insertObject(QJsonObject& object, const QString& key, const QJsonObject& value) {
    if (!value.isEmpty()) {
        object.insert(key, ipcraft::sortedJsonObject(value));
    }
}

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

QJsonArray arrayValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray{};
}

ipcraft::DiagnosticLocation documentLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ipcraft::DiagnosticLocation connectionLocation(const QString& connectionId) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("connection");
    location.connectionId = connectionId;
    return location;
}

ipcraft::DiagnosticLocation interfaceLocation(const QString& instanceId,
                                              const QString& interfaceId) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("interface");
    location.instanceId = instanceId;
    location.interfaceId = interfaceId;
    return location;
}

ipcraft::Diagnostic diagnostic(const QString& ruleId,
                               const QString& category,
                               const QString& message,
                               QVector<ipcraft::DiagnosticLocation> locations) {
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = QStringLiteral("core");
    record.ruleId = ruleId;
    record.category = category;
    record.message = message;
    record.locations = std::move(locations);
    return record;
}

void addCompositionDiagnostic(ipcraft::DiagnosticStore& store,
                              const QString& ruleId,
                              const QString& message,
                              const QString& path,
                              const QString& connectionId = {}) {
    QVector<ipcraft::DiagnosticLocation> locations{documentLocation(path)};
    if (!connectionId.isEmpty()) {
        locations.append(connectionLocation(connectionId));
    }
    store.records.append(diagnostic(ruleId,
                                    QStringLiteral("composition"),
                                    message,
                                    std::move(locations)));
}

void addRequiredInterfaceDiagnostic(ipcraft::DiagnosticStore& store,
                                    const QString& instanceId,
                                    const QString& interfaceId) {
    store.records.append(diagnostic(
        QStringLiteral("composition.required_interface_unconnected"),
        QStringLiteral("composition"),
        QStringLiteral("Required interface is not connected."),
        {interfaceLocation(instanceId, interfaceId)}));
}

void addGraphDiagnostic(ipcraft::DiagnosticStore& store,
                        const QString& ruleId,
                        const QString& message,
                        const QString& path,
                        const QString& graphObjectId = {}) {
    QVector<ipcraft::DiagnosticLocation> locations{documentLocation(path)};
    if (!graphObjectId.isEmpty()) {
        ipcraft::DiagnosticLocation graphObject;
        graphObject.kind = QStringLiteral("graph_object");
        graphObject.graphObjectId = graphObjectId;
        locations.append(graphObject);
    }
    store.records.append(diagnostic(ruleId,
                                    QStringLiteral("graph_config"),
                                    message,
                                    std::move(locations)));
}

QString indexPath(const QString& base, qsizetype index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

QString childPath(const QString& base, const QString& child) {
    return base + QStringLiteral(".") + child;
}

QString endpointPath(qsizetype connectionIndex, qsizetype endpointIndex, const QString& field) {
    return childPath(indexPath(childPath(indexPath(QStringLiteral("$.connections"),
                                                   connectionIndex),
                                         QStringLiteral("endpoints")),
                               endpointIndex),
                     field);
}

QString graphEndpointPath(qsizetype relationshipIndex, qsizetype endpointIndex, const QString& field) {
    return childPath(indexPath(childPath(indexPath(QStringLiteral("$.relationships"),
                                                   relationshipIndex),
                                         QStringLiteral("endpoints")),
                               endpointIndex),
                     field);
}

const ipcraft::PackageInterfaceSpec* findInterface(const ipcraft::PackageSpec& package,
                                                   const QString& interfaceId) {
    const auto it = std::find_if(package.interfaces.cbegin(),
                                 package.interfaces.cend(),
                                 [&](const ipcraft::PackageInterfaceSpec& spec) {
                                     return spec.id == interfaceId;
                                 });
    return it != package.interfaces.cend() ? &(*it) : nullptr;
}

QString normalizeAlias(QString value, const QHash<QString, QString>& aliases) {
    value = value.trimmed();
    for (int depth = 0; depth < 8; ++depth) {
        if (aliases.contains(value)) {
            const QString next = aliases.value(value).trimmed();
            if (next == value) {
                break;
            }
            value = next;
            continue;
        }

        bool matched = false;
        QStringList keys = aliases.keys();
        keys.sort(Qt::CaseInsensitive);
        for (const QString& key : keys) {
            if (QString::compare(key, value, Qt::CaseInsensitive) == 0) {
                const QString next = aliases.value(key).trimmed();
                if (next != value) {
                    value = next;
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            break;
        }
    }
    return value.toLower();
}

QString endpointKey(const QString& instanceId, const QString& interfaceId) {
    return instanceId + QLatin1Char('\n') + interfaceId;
}

bool isSourceRole(const QString& role, const QString& direction) {
    const QString normalizedRole = role.toLower();
    const QString normalizedDirection = direction.toLower();
    return normalizedRole == QStringLiteral("source") ||
           normalizedRole == QStringLiteral("master") ||
           normalizedRole == QStringLiteral("output") ||
           normalizedRole == QStringLiteral("producer") ||
           normalizedRole == QStringLiteral("driver") ||
           normalizedDirection == QStringLiteral("output") ||
           normalizedDirection == QStringLiteral("out") ||
           normalizedDirection == QStringLiteral("source");
}

bool isSinkRole(const QString& role, const QString& direction) {
    const QString normalizedRole = role.toLower();
    const QString normalizedDirection = direction.toLower();
    return normalizedRole == QStringLiteral("sink") ||
           normalizedRole == QStringLiteral("slave") ||
           normalizedRole == QStringLiteral("input") ||
           normalizedRole == QStringLiteral("consumer") ||
           normalizedRole == QStringLiteral("receiver") ||
           normalizedDirection == QStringLiteral("input") ||
           normalizedDirection == QStringLiteral("in") ||
           normalizedDirection == QStringLiteral("sink");
}

struct InstanceContext {
    ipcraft::CompositionInstance instance;
};

struct AliasContext {
    QHash<QString, QString> protocolAliases;
    QHash<QString, QString> kindAliases;
    QVector<ipcraft::PackageCompatibilityRule> compatibility;
};

AliasContext buildAliasContext(const QVector<ipcraft::CompositionInstance>& instances) {
    AliasContext context;
    for (const ipcraft::CompositionInstance& instance : instances) {
        for (auto it = instance.package.connectionRules.protocolAliases.cbegin();
             it != instance.package.connectionRules.protocolAliases.cend();
             ++it) {
            context.protocolAliases.insert(it.key(), it.value());
        }
        for (auto it = instance.package.connectionRules.kindAliases.cbegin();
             it != instance.package.connectionRules.kindAliases.cend();
             ++it) {
            context.kindAliases.insert(it.key(), it.value());
        }
        for (const ipcraft::PackageCompatibilityRule& rule :
             instance.package.connectionRules.compatibility) {
            context.compatibility.append(rule);
        }
    }
    return context;
}

struct ResolvedEndpoint {
    const ipcraft::CompositionEndpointRef* ref = nullptr;
    const ipcraft::PackageInterfaceSpec* interfaceSpec = nullptr;
    QString connectionId;
    QString path;
    QString instanceId;
    QString interfaceId;
    QString kind;
    QString protocol;
    QString role;
    QString direction;
    bool valid = false;
    bool source = false;
    bool sink = false;
};

bool matchesEndpoint(const ipcraft::PackageEndpointMatch& match,
                     const ResolvedEndpoint& endpoint,
                     const AliasContext& aliases) {
    const QString matchKind = normalizeAlias(match.kind, aliases.kindAliases);
    const QString matchProtocol = normalizeAlias(match.protocol, aliases.protocolAliases);
    const QString matchRole = match.role.trimmed().toLower();
    const QString matchDirection = match.direction.trimmed().toLower();

    if (!matchKind.isEmpty() && matchKind != endpoint.kind) {
        return false;
    }
    if (!matchProtocol.isEmpty() && matchProtocol != endpoint.protocol) {
        return false;
    }
    if (!matchRole.isEmpty() && matchRole != endpoint.role) {
        return false;
    }
    if (!matchDirection.isEmpty() && matchDirection != endpoint.direction) {
        return false;
    }
    return true;
}

bool ruleAllows(const ipcraft::PackageCompatibilityRule& rule,
                const QString& connectionType,
                const ResolvedEndpoint& source,
                const ResolvedEndpoint& sink,
                const QString& arity,
                const AliasContext& aliases) {
    if (!rule.connectionType.isEmpty() &&
        rule.connectionType.toLower() != connectionType.toLower()) {
        return false;
    }
    if (!rule.arity.isEmpty() && rule.arity.toLower() != arity.toLower()) {
        return false;
    }
    return matchesEndpoint(rule.from, source, aliases) &&
           matchesEndpoint(rule.to, sink, aliases);
}

bool hasCompatibilityRule(const AliasContext& aliases,
                          const QString& connectionType,
                          const ResolvedEndpoint& source,
                          const ResolvedEndpoint& sink,
                          const QStringList& arities) {
    if (aliases.compatibility.isEmpty()) {
        return source.kind == sink.kind &&
               (source.protocol.isEmpty() || sink.protocol.isEmpty() ||
                source.protocol == sink.protocol) &&
               source.source &&
               sink.sink;
    }

    for (const ipcraft::PackageCompatibilityRule& rule : aliases.compatibility) {
        for (const QString& arity : arities) {
            if (ruleAllows(rule, connectionType, source, sink, arity, aliases)) {
                return true;
            }
        }
    }
    return false;
}

bool hasConnectionClassRule(const AliasContext& aliases,
                            const QString& connectionType) {
    if (aliases.compatibility.isEmpty()) {
        return true;
    }
    for (const ipcraft::PackageCompatibilityRule& rule : aliases.compatibility) {
        if (rule.connectionType.isEmpty() ||
            rule.connectionType.compare(connectionType, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool isClockOrReset(const QString& connectionType) {
    const QString type = connectionType.toLower();
    return type == QStringLiteral("clock") || type == QStringLiteral("reset");
}

bool validateConnectionCompatibility(ipcraft::DiagnosticStore& diagnostics,
                                     const ipcraft::SystemConnection& connection,
                                     qsizetype connectionIndex,
                                     const QVector<ResolvedEndpoint>& endpoints,
                                     const AliasContext& aliases) {
    QVector<ResolvedEndpoint> sources;
    QVector<ResolvedEndpoint> sinks;
    for (const ResolvedEndpoint& endpoint : endpoints) {
        if (!endpoint.valid) {
            return false;
        }
        if (endpoint.source) {
            sources.append(endpoint);
        }
        if (endpoint.sink) {
            sinks.append(endpoint);
        }
    }

    const QString connectionPath = indexPath(QStringLiteral("$.connections"), connectionIndex);
    if (!hasConnectionClassRule(aliases, connection.type)) {
        addCompositionDiagnostic(diagnostics,
                                 QStringLiteral("composition.unknown_connection_class"),
                                 QStringLiteral("Connection type has no declared compatibility rule."),
                                 childPath(connectionPath, QStringLiteral("type")),
                                 connection.id);
        return false;
    }

    if (isClockOrReset(connection.type) && sources.size() != 1) {
        addCompositionDiagnostic(diagnostics,
                                 QStringLiteral("composition.clock_reset_source_count"),
                                 QStringLiteral("Clock/reset fanout must have exactly one source."),
                                 childPath(connectionPath, QStringLiteral("endpoints")),
                                 connection.id);
        return false;
    }

    if (sources.size() > 1 && !sinks.isEmpty() && !isClockOrReset(connection.type)) {
        addCompositionDiagnostic(diagnostics,
                                 QStringLiteral("composition.multiply_driven_input"),
                                 QStringLiteral("Input/sink endpoint is driven by multiple sources."),
                                 childPath(connectionPath, QStringLiteral("endpoints")),
                                 connection.id);
        return false;
    }

    QStringList arities;
    if (connection.endpoints.size() == 2) {
        arities.append(QStringLiteral("binary"));
    }
    arities.append(QStringLiteral("fanout"));

    if (sources.size() != 1 || sinks.isEmpty()) {
        addCompositionDiagnostic(diagnostics,
                                 QStringLiteral("composition.incompatible_endpoint"),
                                 QStringLiteral("Connection endpoints are not source/sink compatible."),
                                 childPath(connectionPath, QStringLiteral("endpoints")),
                                 connection.id);
        return false;
    }

    const ResolvedEndpoint source = sources.first();
    bool compatible = true;
    for (const ResolvedEndpoint& sink : sinks) {
        if (!hasCompatibilityRule(aliases, connection.type, source, sink, arities)) {
            addCompositionDiagnostic(diagnostics,
                                     QStringLiteral("composition.incompatible_endpoint"),
                                     QStringLiteral("Connection endpoints are not compatible."),
                                     sink.path,
                                     connection.id);
            compatible = false;
        }
    }
    return compatible;
}

} // namespace

namespace ipcraft {

QJsonObject CompositionEndpointRef::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("instance"), instanceId);
    object.insert(QStringLiteral("interface"), interfaceId);
    insertString(object, QStringLiteral("port"), portId);
    insertString(object, QStringLiteral("role"), role);
    insertObject(object, QStringLiteral("properties"), properties);
    return sortedJsonObject(object);
}

CompositionEndpointRef CompositionEndpointRef::fromJson(const QJsonObject& object) {
    CompositionEndpointRef ref;
    ref.instanceId = object.value(QStringLiteral("instance")).toString();
    ref.interfaceId = object.value(QStringLiteral("interface")).toString();
    ref.portId = object.value(QStringLiteral("port")).toString();
    ref.role = object.value(QStringLiteral("role")).toString();
    ref.properties = objectValue(object, QStringLiteral("properties"));
    return ref;
}

QJsonObject SystemConnection::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("type"), type);
    QJsonArray endpointArray;
    for (const CompositionEndpointRef& endpoint : endpoints) {
        endpointArray.append(endpoint.toJson());
    }
    object.insert(QStringLiteral("endpoints"), endpointArray);
    if (!source.isEmpty()) {
        object.insert(QStringLiteral("source"), source);
    }
    insertObject(object, QStringLiteral("properties"), properties);
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

SystemConnection SystemConnection::fromJson(const QJsonObject& object) {
    SystemConnection connection;
    connection.id = object.value(QStringLiteral("id")).toString();
    connection.type = object.value(QStringLiteral("type")).toString();
    const QJsonArray endpoints = arrayValue(object, QStringLiteral("endpoints"));
    for (const QJsonValue& endpoint : endpoints) {
        if (endpoint.isObject()) {
            connection.endpoints.append(CompositionEndpointRef::fromJson(endpoint.toObject()));
        }
    }
    connection.source = object.value(QStringLiteral("source")).toString(QStringLiteral("user"));
    connection.properties = objectValue(object, QStringLiteral("properties"));
    connection.native = objectValue(object, QStringLiteral("native"));
    return connection;
}

QJsonObject ExternalPort::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    insertString(object, QStringLiteral("name"), name);
    if (hasInterface) {
        object.insert(QStringLiteral("interface"), interfaceRef.toJson());
    }
    insertObject(object, QStringLiteral("properties"), properties);
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

ExternalPort ExternalPort::fromJson(const QJsonObject& object) {
    ExternalPort port;
    port.id = object.value(QStringLiteral("id")).toString();
    port.name = object.value(QStringLiteral("name")).toString();
    if (object.value(QStringLiteral("interface")).isObject()) {
        port.hasInterface = true;
        port.interfaceRef = CompositionEndpointRef::fromJson(
            object.value(QStringLiteral("interface")).toObject());
    }
    port.properties = objectValue(object, QStringLiteral("properties"));
    port.native = objectValue(object, QStringLiteral("native"));
    return port;
}

QJsonObject CompositionModel::toJson() const {
    QJsonObject object;
    QJsonArray connectionArray;
    for (const SystemConnection& connection : connections) {
        connectionArray.append(connection.toJson());
    }
    object.insert(QStringLiteral("connections"), connectionArray);

    QJsonArray externalPortArray;
    for (const ExternalPort& port : externalPorts) {
        externalPortArray.append(port.toJson());
    }
    object.insert(QStringLiteral("external_ports"), externalPortArray);
    object.insert(QStringLiteral("groups"), groups);
    object.insert(QStringLiteral("properties"), sortedJsonObject(properties));
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

CompositionModel CompositionModel::fromJson(const QJsonObject& object) {
    CompositionModel model;
    const QJsonArray connectionArray = arrayValue(object, QStringLiteral("connections"));
    for (const QJsonValue& connection : connectionArray) {
        if (connection.isObject()) {
            model.connections.append(SystemConnection::fromJson(connection.toObject()));
        }
    }
    const QJsonArray externalPortArray = arrayValue(object, QStringLiteral("external_ports"));
    for (const QJsonValue& port : externalPortArray) {
        if (port.isObject()) {
            model.externalPorts.append(ExternalPort::fromJson(port.toObject()));
        }
    }
    model.groups = arrayValue(object, QStringLiteral("groups"));
    model.properties = objectValue(object, QStringLiteral("properties"));
    model.native = objectValue(object, QStringLiteral("native"));
    return model;
}

CompositionValidationResult validateCompositionModel(
    const CompositionModel& model,
    const QVector<CompositionInstance>& instances) {
    CompositionValidationResult result;
    AliasContext aliases = buildAliasContext(instances);
    QHash<QString, const CompositionInstance*> instanceById;
    for (const CompositionInstance& instance : instances) {
        instanceById.insert(instance.instanceId, &instance);
    }

    QSet<QString> connectedInterfaces;
    QHash<QString, int> sinkDriverCounts;

    for (qsizetype connectionIndex = 0; connectionIndex < model.connections.size(); ++connectionIndex) {
        const SystemConnection& connection = model.connections.at(connectionIndex);
        QVector<ResolvedEndpoint> resolvedEndpoints;
        int sourceCount = 0;

        for (qsizetype endpointIndex = 0; endpointIndex < connection.endpoints.size(); ++endpointIndex) {
            const CompositionEndpointRef& endpoint = connection.endpoints.at(endpointIndex);
            ResolvedEndpoint resolved;
            resolved.ref = &endpoint;
            resolved.connectionId = connection.id;
            resolved.path = indexPath(childPath(indexPath(QStringLiteral("$.connections"),
                                                         connectionIndex),
                                               QStringLiteral("endpoints")),
                                     endpointIndex);
            resolved.instanceId = endpoint.instanceId;
            resolved.interfaceId = endpoint.interfaceId;

            const CompositionInstance* instance = instanceById.value(endpoint.instanceId, nullptr);
            if (!instance) {
                addCompositionDiagnostic(result.diagnostics,
                                         QStringLiteral("composition.unknown_instance"),
                                         QStringLiteral("Endpoint references an unknown instance."),
                                         endpointPath(connectionIndex, endpointIndex, QStringLiteral("instance")),
                                         connection.id);
                resolvedEndpoints.append(resolved);
                continue;
            }

            const PackageInterfaceSpec* interfaceSpec =
                findInterface(instance->package, endpoint.interfaceId);
            if (!interfaceSpec) {
                addCompositionDiagnostic(result.diagnostics,
                                         QStringLiteral("composition.unknown_interface"),
                                         QStringLiteral("Endpoint references an unknown interface."),
                                         endpointPath(connectionIndex, endpointIndex, QStringLiteral("interface")),
                                         connection.id);
                resolvedEndpoints.append(resolved);
                continue;
            }

            resolved.interfaceSpec = interfaceSpec;
            resolved.kind = normalizeAlias(interfaceSpec->kind, aliases.kindAliases);
            resolved.protocol = normalizeAlias(interfaceSpec->protocol, aliases.protocolAliases);
            resolved.role = (endpoint.role.isEmpty() ? interfaceSpec->role : endpoint.role)
                                .trimmed()
                                .toLower();
            resolved.direction = interfaceSpec->direction.trimmed().toLower();
            resolved.source = isSourceRole(resolved.role, resolved.direction);
            resolved.sink = isSinkRole(resolved.role, resolved.direction);
            resolved.valid = true;
            resolvedEndpoints.append(resolved);
            connectedInterfaces.insert(endpointKey(endpoint.instanceId, endpoint.interfaceId));
            if (resolved.source) {
                ++sourceCount;
            }
        }

        const bool connectionCanDrive = validateConnectionCompatibility(result.diagnostics,
                                                                       connection,
                                                                       connectionIndex,
                                                                       resolvedEndpoints,
                                                                       aliases);

        for (const ResolvedEndpoint& endpoint : resolvedEndpoints) {
            if (!connectionCanDrive || !endpoint.valid || !endpoint.sink || sourceCount <= 0) {
                continue;
            }
            const QString key = endpointKey(endpoint.instanceId, endpoint.interfaceId);
            sinkDriverCounts.insert(key, sinkDriverCounts.value(key) + sourceCount);
        }
    }

    for (qsizetype externalIndex = 0; externalIndex < model.externalPorts.size(); ++externalIndex) {
        const ExternalPort& port = model.externalPorts.at(externalIndex);
        if (!port.hasInterface) {
            continue;
        }

        const QString externalPath = indexPath(QStringLiteral("$.external_ports"), externalIndex);
        const CompositionEndpointRef& endpoint = port.interfaceRef;
        const CompositionInstance* instance = instanceById.value(endpoint.instanceId, nullptr);
        if (!instance) {
            addCompositionDiagnostic(result.diagnostics,
                                     QStringLiteral("composition.unknown_instance"),
                                     QStringLiteral("External port references an unknown instance."),
                                     childPath(childPath(externalPath, QStringLiteral("interface")),
                                               QStringLiteral("instance")));
            continue;
        }
        if (!findInterface(instance->package, endpoint.interfaceId)) {
            addCompositionDiagnostic(result.diagnostics,
                                     QStringLiteral("composition.unknown_interface"),
                                     QStringLiteral("External port references an unknown interface."),
                                     childPath(childPath(externalPath, QStringLiteral("interface")),
                                               QStringLiteral("interface")));
            continue;
        }
        connectedInterfaces.insert(endpointKey(endpoint.instanceId, endpoint.interfaceId));
    }

    QSet<QString> multiplyDrivenReported;
    for (qsizetype connectionIndex = 0; connectionIndex < model.connections.size(); ++connectionIndex) {
        const SystemConnection& connection = model.connections.at(connectionIndex);
        for (qsizetype endpointIndex = 0; endpointIndex < connection.endpoints.size(); ++endpointIndex) {
            const CompositionEndpointRef& endpoint = connection.endpoints.at(endpointIndex);
            const QString key = endpointKey(endpoint.instanceId, endpoint.interfaceId);
            if (sinkDriverCounts.value(key) > 1 && !multiplyDrivenReported.contains(key)) {
                addCompositionDiagnostic(result.diagnostics,
                                         QStringLiteral("composition.multiply_driven_input"),
                                         QStringLiteral("Input/sink endpoint is driven by multiple sources."),
                                         endpointPath(connectionIndex, endpointIndex, QStringLiteral("interface")),
                                         connection.id);
                multiplyDrivenReported.insert(key);
            }
        }
    }

    for (const CompositionInstance& instance : instances) {
        for (const PackageInterfaceSpec& interfaceSpec : instance.package.interfaces) {
            if (interfaceSpec.required &&
                !connectedInterfaces.contains(endpointKey(instance.instanceId, interfaceSpec.id))) {
                addRequiredInterfaceDiagnostic(result.diagnostics, instance.instanceId, interfaceSpec.id);
            }
        }
    }

    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

QJsonObject GraphConfigObject::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("type"), type);
    insertObject(object, QStringLiteral("properties"), properties);
    return sortedJsonObject(object);
}

GraphConfigObject GraphConfigObject::fromJson(const QJsonObject& object) {
    GraphConfigObject graphObject;
    graphObject.id = object.value(QStringLiteral("id")).toString();
    graphObject.type = object.value(QStringLiteral("type")).toString();
    graphObject.properties = objectValue(object, QStringLiteral("properties"));
    return graphObject;
}

QJsonObject GraphConfigEndpoint::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("object"), objectId);
    object.insert(QStringLiteral("role"), role);
    insertObject(object, QStringLiteral("properties"), properties);
    return sortedJsonObject(object);
}

GraphConfigEndpoint GraphConfigEndpoint::fromJson(const QJsonObject& object) {
    GraphConfigEndpoint endpoint;
    endpoint.objectId = object.value(QStringLiteral("object")).toString();
    endpoint.role = object.value(QStringLiteral("role")).toString();
    endpoint.properties = objectValue(object, QStringLiteral("properties"));
    return endpoint;
}

QJsonObject GraphConfigRelationship::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("type"), type);
    QJsonArray endpointArray;
    for (const GraphConfigEndpoint& endpoint : endpoints) {
        endpointArray.append(endpoint.toJson());
    }
    object.insert(QStringLiteral("endpoints"), endpointArray);
    insertObject(object, QStringLiteral("properties"), properties);
    return sortedJsonObject(object);
}

GraphConfigRelationship GraphConfigRelationship::fromJson(const QJsonObject& object) {
    GraphConfigRelationship relationship;
    relationship.id = object.value(QStringLiteral("id")).toString();
    relationship.type = object.value(QStringLiteral("type")).toString();
    const QJsonArray endpoints = arrayValue(object, QStringLiteral("endpoints"));
    for (const QJsonValue& endpoint : endpoints) {
        if (endpoint.isObject()) {
            relationship.endpoints.append(GraphConfigEndpoint::fromJson(endpoint.toObject()));
        }
    }
    relationship.properties = objectValue(object, QStringLiteral("properties"));
    return relationship;
}

bool validateRequiredGraphString(const QJsonObject& object,
                                 const QString& key,
                                 const QString& path,
                                 DiagnosticStore& diagnostics) {
    const QJsonValue value = object.value(key);
    if (value.isString() && !value.toString().trimmed().isEmpty()) {
        return true;
    }
    addGraphDiagnostic(diagnostics,
                       QStringLiteral("graph_config.type_mismatch"),
                       QStringLiteral("Graph config required string field is missing or invalid."),
                       childPath(path, key));
    return false;
}

bool validateOptionalGraphObject(const QJsonObject& object,
                                 const QString& key,
                                 const QString& path,
                                 DiagnosticStore& diagnostics) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return true;
    }
    if (value.isObject()) {
        return true;
    }
    addGraphDiagnostic(diagnostics,
                       QStringLiteral("graph_config.type_mismatch"),
                       QStringLiteral("Graph config field must be an object."),
                       childPath(path, key));
    return false;
}

bool validateGraphConfigObjectShape(const QJsonObject& object,
                                    qsizetype objectIndex,
                                    DiagnosticStore& diagnostics) {
    const QString objectPath = indexPath(QStringLiteral("$.objects"), objectIndex);
    bool ok = true;
    const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("type"),
        QStringLiteral("properties")
    };
    for (const QString& key : object.keys()) {
        if (!allowedKeys.contains(key)) {
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph object contains an unsupported field."),
                               childPath(objectPath, key));
            ok = false;
        }
    }
    ok = validateRequiredGraphString(object, QStringLiteral("id"), objectPath, diagnostics) && ok;
    ok = validateRequiredGraphString(object, QStringLiteral("type"), objectPath, diagnostics) && ok;
    ok = validateOptionalGraphObject(object, QStringLiteral("properties"), objectPath, diagnostics) && ok;
    return ok;
}

bool validateGraphConfigEndpointShape(const QJsonObject& object,
                                      const QString& endpointPath,
                                      DiagnosticStore& diagnostics) {
    bool ok = true;
    const QSet<QString> allowedKeys = {
        QStringLiteral("object"),
        QStringLiteral("role"),
        QStringLiteral("properties")
    };
    for (const QString& key : object.keys()) {
        if (!allowedKeys.contains(key)) {
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph relationship endpoint contains an unsupported field."),
                               childPath(endpointPath, key));
            ok = false;
        }
    }
    ok = validateRequiredGraphString(object, QStringLiteral("object"), endpointPath, diagnostics) && ok;
    ok = validateRequiredGraphString(object, QStringLiteral("role"), endpointPath, diagnostics) && ok;
    ok = validateOptionalGraphObject(object, QStringLiteral("properties"), endpointPath, diagnostics) && ok;
    return ok;
}

bool validateGraphConfigRelationshipShape(const QJsonObject& object,
                                          qsizetype relationshipIndex,
                                          DiagnosticStore& diagnostics) {
    const QString relationshipPath = indexPath(QStringLiteral("$.relationships"), relationshipIndex);
    bool ok = true;
    const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("type"),
        QStringLiteral("endpoints"),
        QStringLiteral("properties")
    };
    for (const QString& key : object.keys()) {
        if (!allowedKeys.contains(key)) {
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph relationship contains an unsupported field."),
                               childPath(relationshipPath, key));
            ok = false;
        }
    }
    ok = validateRequiredGraphString(object, QStringLiteral("id"), relationshipPath, diagnostics) && ok;
    ok = validateRequiredGraphString(object, QStringLiteral("type"), relationshipPath, diagnostics) && ok;
    ok = validateOptionalGraphObject(object, QStringLiteral("properties"), relationshipPath, diagnostics) && ok;
    if (!object.value(QStringLiteral("endpoints")).isArray()) {
        addGraphDiagnostic(diagnostics,
                           QStringLiteral("graph_config.type_mismatch"),
                           QStringLiteral("Graph relationship endpoints must be an array."),
                           childPath(relationshipPath, QStringLiteral("endpoints")));
        return false;
    }

    const QJsonArray endpoints = object.value(QStringLiteral("endpoints")).toArray();
    if (endpoints.size() < 2) {
        addGraphDiagnostic(diagnostics,
                           QStringLiteral("graph_config.type_mismatch"),
                           QStringLiteral("Graph relationship endpoints must contain at least two entries."),
                           childPath(relationshipPath, QStringLiteral("endpoints")));
        ok = false;
    }
    for (qsizetype endpointIndex = 0; endpointIndex < endpoints.size(); ++endpointIndex) {
        const QString endpointPath =
            indexPath(childPath(relationshipPath, QStringLiteral("endpoints")), endpointIndex);
        if (!endpoints.at(endpointIndex).isObject()) {
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph relationship endpoint must be an object."),
                               endpointPath);
            ok = false;
            continue;
        }
        ok = validateGraphConfigEndpointShape(endpoints.at(endpointIndex).toObject(),
                                              endpointPath,
                                              diagnostics) && ok;
    }
    return ok;
}

QJsonObject GraphConfig::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schema.isEmpty() ? schemaids::graphConfigV1 : schema);
    QJsonArray objectArray;
    for (const GraphConfigObject& graphObject : objects) {
        objectArray.append(graphObject.toJson());
    }
    object.insert(QStringLiteral("objects"), objectArray);
    QJsonArray relationshipArray;
    for (const GraphConfigRelationship& relationship : relationships) {
        relationshipArray.append(relationship.toJson());
    }
    object.insert(QStringLiteral("relationships"), relationshipArray);
    insertObject(object, QStringLiteral("properties"), properties);
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

GraphConfigReadResult GraphConfig::fromJson(const QJsonObject& object) {
    GraphConfigReadResult result;
    const QSet<QString> allowedTopLevelKeys{
        QStringLiteral("schema"),
        QStringLiteral("objects"),
        QStringLiteral("relationships"),
        QStringLiteral("properties"),
        QStringLiteral("native")
    };
    for (const QString& key : object.keys()) {
        if (!allowedTopLevelKeys.contains(key)) {
            addGraphDiagnostic(result.diagnostics,
                               QStringLiteral("graph_config.unknown_top_level_field"),
                               QStringLiteral("Graph config contains an unsupported top-level field."),
                               childPath(QStringLiteral("$"), key));
            addGraphDiagnostic(result.diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph config contains an unsupported top-level field."),
                               childPath(QStringLiteral("$"), key));
        }
    }

    result.config.schema = object.value(QStringLiteral("schema")).toString();
    if (result.config.schema != schemaids::graphConfigV1) {
        addGraphDiagnostic(result.diagnostics,
                           QStringLiteral("graph_config.unsupported_schema"),
                           QStringLiteral("Graph config schema is not supported."),
                           QStringLiteral("$.schema"));
    }

    validateOptionalGraphObject(object,
                                QStringLiteral("properties"),
                                QStringLiteral("$"),
                                result.diagnostics);
    validateOptionalGraphObject(object,
                                QStringLiteral("native"),
                                QStringLiteral("$"),
                                result.diagnostics);

    const QJsonValue objectsValue = object.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        addGraphDiagnostic(result.diagnostics,
                           QStringLiteral("graph_config.type_mismatch"),
                           QStringLiteral("Graph config objects must be an array."),
                           QStringLiteral("$.objects"));
    } else {
        const QJsonArray objects = objectsValue.toArray();
        for (qsizetype index = 0; index < objects.size(); ++index) {
            const QJsonValue graphObject = objects.at(index);
            const QString objectPath = indexPath(QStringLiteral("$.objects"), index);
            if (!graphObject.isObject()) {
                addGraphDiagnostic(result.diagnostics,
                                   QStringLiteral("graph_config.type_mismatch"),
                                   QStringLiteral("Graph object must be an object."),
                                   objectPath);
                continue;
            }
            const QJsonObject graphObjectObject = graphObject.toObject();
            if (validateGraphConfigObjectShape(graphObjectObject, index, result.diagnostics)) {
                result.config.objects.append(GraphConfigObject::fromJson(graphObjectObject));
            }
        }
    }

    const QJsonValue relationshipsValue = object.value(QStringLiteral("relationships"));
    if (!relationshipsValue.isArray()) {
        addGraphDiagnostic(result.diagnostics,
                           QStringLiteral("graph_config.type_mismatch"),
                           QStringLiteral("Graph config relationships must be an array."),
                           QStringLiteral("$.relationships"));
    } else {
        const QJsonArray relationships = relationshipsValue.toArray();
        for (qsizetype index = 0; index < relationships.size(); ++index) {
            const QJsonValue relationship = relationships.at(index);
            if (relationship.isObject()) {
                const QJsonObject relationshipObject = relationship.toObject();
                if (validateGraphConfigRelationshipShape(relationshipObject,
                                                         index,
                                                         result.diagnostics)) {
                    result.config.relationships.append(
                        GraphConfigRelationship::fromJson(relationshipObject));
                }
            } else {
                addGraphDiagnostic(result.diagnostics,
                                   QStringLiteral("graph_config.type_mismatch"),
                                   QStringLiteral("Graph relationship must be an object."),
                                   indexPath(QStringLiteral("$.relationships"), index));
            }
        }
    }

    result.config.properties = objectValue(object, QStringLiteral("properties"));
    result.config.native = objectValue(object, QStringLiteral("native"));
    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

DiagnosticStore validateGraphConfig(const GraphConfig& graphConfig) {
    DiagnosticStore diagnostics;
    QSet<QString> objectIds;
    for (qsizetype index = 0; index < graphConfig.objects.size(); ++index) {
        const GraphConfigObject& graphObject = graphConfig.objects.at(index);
        if (objectIds.contains(graphObject.id)) {
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.duplicate_object"),
                               QStringLiteral("Graph object id is duplicated."),
                               childPath(indexPath(QStringLiteral("$.objects"), index),
                                         QStringLiteral("id")),
                               graphObject.id);
            continue;
        }
        objectIds.insert(graphObject.id);
    }

    QSet<QString> relationshipIds;
    for (qsizetype relationshipIndex = 0;
         relationshipIndex < graphConfig.relationships.size();
         ++relationshipIndex) {
        const GraphConfigRelationship& relationship =
            graphConfig.relationships.at(relationshipIndex);
        if (relationshipIds.contains(relationship.id)) {
            const QString idPath = childPath(indexPath(QStringLiteral("$.relationships"),
                                                       relationshipIndex),
                                             QStringLiteral("id"));
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.duplicate_relationship"),
                               QStringLiteral("Graph relationship id is duplicated."),
                               idPath);
            addGraphDiagnostic(diagnostics,
                               QStringLiteral("graph_config.type_mismatch"),
                               QStringLiteral("Graph relationship id is duplicated."),
                               idPath);
        } else {
            relationshipIds.insert(relationship.id);
        }
        for (qsizetype endpointIndex = 0; endpointIndex < relationship.endpoints.size(); ++endpointIndex) {
            const GraphConfigEndpoint& endpoint = relationship.endpoints.at(endpointIndex);
            if (!objectIds.contains(endpoint.objectId)) {
                addGraphDiagnostic(diagnostics,
                                   QStringLiteral("graph_config.unknown_endpoint_object"),
                                   QStringLiteral("Graph relationship endpoint references an unknown object."),
                                   graphEndpointPath(relationshipIndex,
                                                     endpointIndex,
                                                     QStringLiteral("object")),
                                   endpoint.objectId);
            }
        }
    }
    return diagnostics;
}

} // namespace ipcraft
