#include "connection/connectionruleservice.h"

#include "common/portlayout.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <variant>

namespace {

QString directionName(Port::Direction direction) {
    if (direction == Port::Direction::Input) {
        return QStringLiteral("input");
    }
    if (direction == Port::Direction::Output) {
        return QStringLiteral("output");
    }
    return QStringLiteral("inout");
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }
    for (const Port& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }
    return nullptr;
}

bool connectionExists(const Graph* graph, const PortRef& source, const PortRef& target) {
    if (!graph) {
        return false;
    }
    return std::any_of(graph->connections().begin(), graph->connections().end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return connection->source().moduleId == source.moduleId &&
                   connection->source().portId == source.portId &&
                   connection->target().moduleId == target.moduleId &&
                   connection->target().portId == target.portId;
        });
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
    if (field == QStringLiteral("protocol") &&
        value.compare(QStringLiteral("axi"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("axi4");
    }
    return value;
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

bool metadataCompatible(const PortSemanticInfo& source,
                        const PortSemanticInfo& target,
                        QString* reason,
                        QString* message) {
    const QString sourceBus = source.interfaceBus.isEmpty() ? source.busType : source.interfaceBus;
    const QString targetBus = target.interfaceBus.isEmpty() ? target.busType : target.interfaceBus;
    if (sourceBus != targetBus) {
        if (reason) *reason = QStringLiteral("bus_mismatch");
        if (message) *message = QStringLiteral("Connection bus types do not match");
        return false;
    }

    if (!source.interfaceRole.isEmpty() || !target.interfaceRole.isEmpty()) {
        if (!source.compatibleRoles.contains(target.interfaceRole) ||
            !target.compatibleRoles.contains(source.interfaceRole)) {
            if (reason) *reason = QStringLiteral("interface_role_mismatch");
            if (message) *message = QStringLiteral("Connection interface roles are not compatible");
            return false;
        }
    }

    QStringList fields = source.matchFieldValues.keys();
    for (const QString& field : target.matchFieldValues.keys()) {
        if (!fields.contains(field)) {
            fields.append(field);
        }
    }
    for (const QString& field : fields) {
        if (!valuesOverlap(source.matchFieldValues.value(field),
                           target.matchFieldValues.value(field))) {
            if (reason) *reason = QStringLiteral("interface_field_mismatch");
            if (message) *message = QStringLiteral("Connection interface field values do not overlap");
            return false;
        }
    }

    return true;
}

bool portOccupiedForCardinalityOne(const Graph* graph, const PortRef& ref) {
    if (!graph) {
        return false;
    }
    return std::any_of(graph->connections().begin(), graph->connections().end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return (connection->source().moduleId == ref.moduleId &&
                    connection->source().portId == ref.portId) ||
                   (connection->target().moduleId == ref.moduleId &&
                    connection->target().portId == ref.portId);
        });
}

bool oppositeSideRulePasses(const PortSemanticInfo& source,
                            const PortSemanticInfo& target) {
    if (source.topologyRule != QStringLiteral("opposite_side") &&
        target.topologyRule != QStringLiteral("opposite_side")) {
        return true;
    }

    const QString sourceSide = PortLayout::routerSideId(source.ref.portId);
    const QString targetSide = PortLayout::routerSideId(target.ref.portId);
    return !sourceSide.isEmpty() &&
           PortLayout::oppositeRouterSide(sourceSide) == targetSide;
}

} // namespace

ConnectionRequest ConnectionRequest::portToPort(const PortRef& start,
                                                const PortRef& end,
                                                ConnectionRequestKind requestKind) {
    ConnectionRequest request;
    request.kind = requestKind;
    request.interactive = requestKind != ConnectionRequestKind::ProjectLoad;
    request.start.moduleId = start.moduleId;
    request.start.portId = start.portId;
    request.end.moduleId = end.moduleId;
    request.end.portId = end.portId;
    return request;
}

ConnectionRuleService::ConnectionRuleService(const Graph* graph,
                                             QVector<ProjectPluginStateRecord> pluginStates)
    : m_graph(graph),
      m_pluginStates(std::move(pluginStates)) {}

ConnectionCheckResult ConnectionRuleService::reject(QString reasonCode, QString message) const {
    ConnectionCheckResult result;
    result.status = ConnectionCheckStatus::Rejected;
    result.reasonCode = std::move(reasonCode);
    result.message = std::move(message);
    return result;
}

std::optional<PortSemanticInfo> ConnectionRuleService::resolvePort(const QString& moduleId,
                                                                   const QString& portId,
                                                                   bool visibleInUi) const {
    const Module* module = m_graph ? m_graph->getModule(moduleId) : nullptr;
    const Port* port = findPort(module, portId);
    if (!module || !port) {
        return std::nullopt;
    }

    const ModuleType* moduleType = ModuleTypeMetadata::type(module);
    PortSemanticInfo info;
    info.ref = PortRef{moduleId, portId};
    info.moduleType = module->type();
    info.pluginId = moduleType ? moduleType->pluginId : QString();
    info.graphGroup = moduleType ? moduleType->graphGroup : QString();
    info.editorLayout = ModuleTypeMetadata::editorLayout(module);
    info.portName = port->name();
    info.direction = directionName(port->direction());
    info.busType = port->busType();
    info.portRole = port->role();
    info.interfaceId = port->interfaceId();
    info.supportsInput = PortLayout::supportsInput(*port);
    info.supportsOutput = PortLayout::supportsOutput(*port);
    info.visibleInUi = visibleInUi;
    info.occupiedAsSource = portOccupiedForCardinalityOne(m_graph, info.ref);
    info.occupiedAsTarget = info.occupiedAsSource;

    if (moduleType && !port->interfaceId().isEmpty()) {
        const auto metadataIt = moduleType->interfaceMetadata.find(port->interfaceId());
        if (metadataIt != moduleType->interfaceMetadata.end()) {
            const ModuleInterfaceMetadata& metadata = metadataIt.value();
            info.interfaceBus = metadata.bus;
            info.interfaceRole = metadata.role;
            info.compatibleRoles = metadata.compatibleRoles;
            info.cardinality = metadata.cardinality.isEmpty() ? QStringLiteral("one")
                                                              : metadata.cardinality;
            info.autocompleteGroup = metadata.autocompleteGroup;
            info.topologyRule = metadata.topologyRule;
            QStringList fields = metadata.matchFields;
            fields.sort();
            for (const QString& field : fields) {
                info.matchFieldValues.insert(field, interfaceFieldValues(metadata, module, field));
            }
        }
    }
    return info;
}

QVector<PortSemanticInfo> ConnectionRuleService::resolveEndpointPorts(
    const ConnectionEndpointRequest& endpoint) const {
    QVector<PortSemanticInfo> ports;
    const Module* module = m_graph ? m_graph->getModule(endpoint.moduleId) : nullptr;
    if (!module) {
        return ports;
    }

    if (endpoint.portId.has_value()) {
        if (auto info = resolvePort(endpoint.moduleId, *endpoint.portId, !endpoint.fromNodeBody)) {
            ports.push_back(*info);
        }
        return ports;
    }

    if (!endpoint.fromNodeBody || !endpoint.hiddenPortsAllowed) {
        return ports;
    }

    for (const Port& port : module->ports()) {
        if (auto info = resolvePort(endpoint.moduleId, port.id(), false)) {
            ports.push_back(*info);
        }
    }
    return ports;
}

QVector<ConnectionResolvedOption> ConnectionRuleService::buildOptions(
    const QVector<PortSemanticInfo>& startPorts,
    const QVector<PortSemanticInfo>& endPorts,
    const ConnectionRequest&,
    QString* rejectionReason,
    QString* rejectionMessage) const {
    QVector<ConnectionResolvedOption> options;
    for (const PortSemanticInfo& start : startPorts) {
        for (const PortSemanticInfo& end : endPorts) {
            if (start.ref.moduleId == end.ref.moduleId) {
                if (rejectionReason) *rejectionReason = QStringLiteral("self_loop");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Cannot connect a module to itself");
                continue;
            }

            if (!start.supportsOutput || !end.supportsInput) {
                if (rejectionReason) *rejectionReason = QStringLiteral("direction_mismatch");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("No direction-compatible connection option");
                continue;
            }

            if (connectionExists(m_graph, start.ref, end.ref)) {
                if (rejectionReason) *rejectionReason = QStringLiteral("duplicate_connection");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection already exists");
                continue;
            }

            if ((start.cardinality == QStringLiteral("one") &&
                 portOccupiedForCardinalityOne(m_graph, start.ref)) ||
                (end.cardinality == QStringLiteral("one") &&
                 portOccupiedForCardinalityOne(m_graph, end.ref))) {
                if (rejectionReason) *rejectionReason = QStringLiteral("port_occupied");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection port is already occupied");
                continue;
            }

            if (!oppositeSideRulePasses(start, end)) {
                if (rejectionReason) *rejectionReason = QStringLiteral("topology_rule_mismatch");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection does not satisfy topology rule");
                continue;
            }

            if (!metadataCompatible(start, end, rejectionReason, rejectionMessage)) {
                continue;
            }

            options.push_back(ConnectionResolvedOption{
                start.ref,
                end.ref,
                QStringLiteral("%1.%2 -> %3.%4")
                    .arg(start.ref.moduleId, start.ref.portId, end.ref.moduleId, end.ref.portId),
                0
            });
        }
    }
    if (options.isEmpty() && rejectionReason && rejectionReason->isEmpty()) {
        *rejectionReason = QStringLiteral("direction_mismatch");
        if (rejectionMessage) *rejectionMessage = QStringLiteral("No direction-compatible connection option");
    }
    return options;
}

ConnectionCheckResult ConnectionRuleService::check(const ConnectionRequest& request) const {
    if (!m_graph) {
        return reject(QStringLiteral("missing_graph"), QStringLiteral("Connection graph is not available"));
    }
    if (!m_graph->getModule(request.start.moduleId) || !m_graph->getModule(request.end.moduleId)) {
        return reject(QStringLiteral("missing_module"), QStringLiteral("Connection references a missing module"));
    }

    const QVector<PortSemanticInfo> startPorts = resolveEndpointPorts(request.start);
    const QVector<PortSemanticInfo> endPorts = resolveEndpointPorts(request.end);
    if (startPorts.isEmpty() || endPorts.isEmpty()) {
        return reject(QStringLiteral("missing_port"), QStringLiteral("Connection references a missing port"));
    }

    QString reason;
    QString message;
    QVector<ConnectionResolvedOption> options = buildOptions(startPorts, endPorts, request, &reason, &message);
    if (options.isEmpty()) {
        return reject(reason.isEmpty() ? QStringLiteral("no_connection_option") : reason,
                      message.isEmpty() ? QStringLiteral("No legal connection option") : message);
    }

    std::sort(options.begin(), options.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority < rhs.priority || (lhs.priority == rhs.priority && lhs.label < rhs.label);
    });

    ConnectionCheckResult result;
    result.status = options.size() == 1 ? ConnectionCheckStatus::Allowed
                                        : ConnectionCheckStatus::NeedsSelection;
    result.options = std::move(options);
    return result;
}
