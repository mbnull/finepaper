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

bool portOccupiedForCardinalityOne(const Graph* graph, const PortRef& ref);
bool oppositeSideRulePasses(const PortSemanticInfo& source, const PortSemanticInfo& target);
bool endpointAllowsAsSource(const ConnectionEndpointRequest& endpoint);
bool endpointAllowsAsTarget(const ConnectionEndpointRequest& endpoint);
bool roleAllowsAsSource(const QString& role);
bool roleAllowsAsTarget(const QString& role);
bool endpointsAreBidirectionalPeerLink(const PortSemanticInfo& source, const PortSemanticInfo& target);

struct CandidateEvaluation {
    bool accepted = false;
    ConnectionRuleLayer layer = ConnectionRuleLayer::FeaturePlugin;
    QString reasonCode;
    QString message;
};

CandidateEvaluation acceptedCandidate() {
    CandidateEvaluation evaluation;
    evaluation.accepted = true;
    evaluation.layer = ConnectionRuleLayer::Ipcore;
    return evaluation;
}

CandidateEvaluation rejectedCandidate(ConnectionRuleLayer layer,
                                      QString reasonCode,
                                      QString message) {
    CandidateEvaluation evaluation;
    evaluation.layer = layer;
    evaluation.reasonCode = std::move(reasonCode);
    evaluation.message = std::move(message);
    return evaluation;
}

CandidateEvaluation checkFeatureDeclarativeRules(const Graph* graph,
                                                 const PortSemanticInfo& source,
                                                 const PortSemanticInfo& target,
                                                 const ConnectionEndpointRequest& sourceEndpoint,
                                                 const ConnectionEndpointRequest& targetEndpoint) {
    if (!endpointAllowsAsSource(sourceEndpoint) || !endpointAllowsAsTarget(targetEndpoint)) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("direction_mismatch"),
                                 QStringLiteral("No direction-compatible connection option"));
    }

    if (!source.supportsOutput || !target.supportsInput) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("direction_mismatch"),
                                 QStringLiteral("No direction-compatible connection option"));
    }

    if ((source.cardinality == QStringLiteral("one") &&
         portOccupiedForCardinalityOne(graph, source.ref)) ||
        (target.cardinality == QStringLiteral("one") &&
         portOccupiedForCardinalityOne(graph, target.ref))) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("port_occupied"),
                                 QStringLiteral("Connection port is already occupied"));
    }

    if (!oppositeSideRulePasses(source, target)) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("topology_rule_mismatch"),
                                 QStringLiteral("Connection does not satisfy topology rule"));
    }

    const QString sourceBus = source.interfaceBus.isEmpty() ? source.busType : source.interfaceBus;
    const QString targetBus = target.interfaceBus.isEmpty() ? target.busType : target.interfaceBus;
    if (sourceBus != targetBus) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("bus_mismatch"),
                                 QStringLiteral("Connection bus types do not match"));
    }

    if (!endpointsAreBidirectionalPeerLink(source, target) &&
        (!roleAllowsAsSource(source.interfaceRole) ||
         !roleAllowsAsTarget(target.interfaceRole))) {
        return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                 QStringLiteral("interface_role_mismatch"),
                                 QStringLiteral("Connection interface roles are not compatible"));
    }

    if (!source.interfaceRole.isEmpty() || !target.interfaceRole.isEmpty()) {
        if (!source.compatibleRoles.contains(target.interfaceRole) ||
            !target.compatibleRoles.contains(source.interfaceRole)) {
            return rejectedCandidate(ConnectionRuleLayer::FeaturePlugin,
                                     QStringLiteral("interface_role_mismatch"),
                                     QStringLiteral("Connection interface roles are not compatible"));
        }
    }

    return acceptedCandidate();
}

CandidateEvaluation checkIpcoreDeclarativeConstraints(const PortSemanticInfo& source,
                                                      const PortSemanticInfo& target) {
    if (!source.ipcoreId.isEmpty() &&
        !target.ipcoreId.isEmpty() &&
        source.ipcoreId != target.ipcoreId) {
        return rejectedCandidate(ConnectionRuleLayer::Ipcore,
                                 QStringLiteral("ipcore_mismatch"),
                                 QStringLiteral("Connection endpoints belong to different IP cores"));
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
            return rejectedCandidate(ConnectionRuleLayer::Ipcore,
                                     QStringLiteral("interface_field_mismatch"),
                                     QStringLiteral("Connection interface field values do not overlap"));
        }
    }

    return acceptedCandidate();
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

bool endpointAllowsAsSource(const ConnectionEndpointRequest& endpoint) {
    return endpoint.visualSide != ConnectionVisualSide::Input;
}

bool endpointAllowsAsTarget(const ConnectionEndpointRequest& endpoint) {
    return endpoint.visualSide != ConnectionVisualSide::Output;
}

bool roleAllowsAsSource(const QString& role) {
    if (role.isEmpty()) {
        return true;
    }
    return role != QStringLiteral("target");
}

bool roleAllowsAsTarget(const QString& role) {
    if (role.isEmpty()) {
        return true;
    }
    return role != QStringLiteral("initiator");
}

bool endpointsAreBidirectionalPeerLink(const PortSemanticInfo& source,
                                       const PortSemanticInfo& target) {
    return source.supportsInput && source.supportsOutput &&
           target.supportsInput && target.supportsOutput &&
           (source.topologyRule == QStringLiteral("opposite_side") ||
            target.topologyRule == QStringLiteral("opposite_side"));
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
                                             QVector<ProjectIpInstanceRecord> ipInstanceRecords)
    : m_graph(graph),
      m_ipInstanceRecords(std::move(ipInstanceRecords)) {}

ConnectionCheckResult ConnectionRuleService::reject(ConnectionRuleLayer layer,
                                                    QString reasonCode,
                                                    QString message) const {
    ConnectionCheckResult result;
    result.status = ConnectionCheckStatus::Rejected;
    result.layer = layer;
    result.reasonCode = std::move(reasonCode);
    result.message = std::move(message);
    return result;
}

std::optional<ConnectionCheckResult>
ConnectionRuleService::checkStructuralRules(const ConnectionRequest& request) const {
    if (!m_graph) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("missing_graph"),
                      QStringLiteral("Connection graph is not available"));
    }
    if (!m_graph->getModule(request.start.moduleId) || !m_graph->getModule(request.end.moduleId)) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("missing_module"),
                      QStringLiteral("Connection references a missing module"));
    }
    if (request.start.moduleId == request.end.moduleId) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("self_loop"),
                      QStringLiteral("Cannot connect a module to itself"));
    }
    if (request.start.portId.has_value() && !findPort(m_graph->getModule(request.start.moduleId), *request.start.portId)) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("missing_port"),
                      QStringLiteral("Connection references a missing port"));
    }
    if (request.end.portId.has_value() && !findPort(m_graph->getModule(request.end.moduleId), *request.end.portId)) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("missing_port"),
                      QStringLiteral("Connection references a missing port"));
    }
    if (request.start.portId.has_value() && request.end.portId.has_value()) {
        const PortRef source{request.start.moduleId, *request.start.portId};
        const PortRef target{request.end.moduleId, *request.end.portId};
        if (connectionExists(m_graph, source, target)) {
            return reject(ConnectionRuleLayer::Structural,
                          QStringLiteral("duplicate_connection"),
                          QStringLiteral("Connection already exists"));
        }
    }
    return std::nullopt;
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
    info.ipcoreId = !module->ipcoreId().isEmpty()
        ? module->ipcoreId()
        : (moduleType ? moduleType->ipcoreId : QString());
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
    const ConnectionRequest& request,
    ConnectionRuleLayer* rejectionLayer,
    QString* rejectionReason,
    QString* rejectionMessage) const {
    QVector<ConnectionResolvedOption> options;
    const bool allowReverse =
        request.kind == ConnectionRequestKind::PortToNode ||
        request.kind == ConnectionRequestKind::NodeToPort;
    const auto optionExists = [&](const PortRef& source, const PortRef& target) {
        return std::any_of(options.cbegin(), options.cend(),
            [&](const ConnectionResolvedOption& option) {
                return option.source.moduleId == source.moduleId &&
                       option.source.portId == source.portId &&
                       option.target.moduleId == target.moduleId &&
                       option.target.portId == target.portId;
            });
    };

    const auto filterAutocompleteCandidates = [](const QVector<PortSemanticInfo>& fixedPorts,
                                                 const QVector<PortSemanticInfo>& hiddenPorts) {
        QStringList groups;
        for (const PortSemanticInfo& port : fixedPorts) {
            if (!port.autocompleteGroup.isEmpty() && !groups.contains(port.autocompleteGroup)) {
                groups.append(port.autocompleteGroup);
            }
        }
        if (groups.isEmpty()) {
            return hiddenPorts;
        }

        QVector<PortSemanticInfo> filtered;
        for (const PortSemanticInfo& port : hiddenPorts) {
            if (groups.contains(port.autocompleteGroup)) {
                filtered.push_back(port);
            }
        }
        return filtered.isEmpty() ? hiddenPorts : filtered;
    };

    const QVector<PortSemanticInfo> effectiveStartPorts =
        request.start.fromNodeBody ? filterAutocompleteCandidates(endPorts, startPorts) : startPorts;
    const QVector<PortSemanticInfo> effectiveEndPorts =
        request.end.fromNodeBody ? filterAutocompleteCandidates(startPorts, endPorts) : endPorts;

    const auto tryAppendOption = [&](const PortSemanticInfo& source,
                                     const PortSemanticInfo& target,
                                     const ConnectionEndpointRequest& sourceEndpoint,
                                     const ConnectionEndpointRequest& targetEndpoint) {
        if (connectionExists(m_graph, source.ref, target.ref)) {
            if (rejectionLayer) *rejectionLayer = ConnectionRuleLayer::Structural;
            if (rejectionReason) *rejectionReason = QStringLiteral("duplicate_connection");
            if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection already exists");
            return;
        }

        const CandidateEvaluation feature =
            checkFeatureDeclarativeRules(m_graph, source, target, sourceEndpoint, targetEndpoint);
        if (!feature.accepted) {
            if (rejectionLayer) *rejectionLayer = feature.layer;
            if (rejectionReason) *rejectionReason = feature.reasonCode;
            if (rejectionMessage) *rejectionMessage = feature.message;
            return;
        }

        const CandidateEvaluation ipcore = checkIpcoreDeclarativeConstraints(source, target);
        if (!ipcore.accepted) {
            if (rejectionLayer) *rejectionLayer = ipcore.layer;
            if (rejectionReason) *rejectionReason = ipcore.reasonCode;
            if (rejectionMessage) *rejectionMessage = ipcore.message;
            return;
        }

        if (optionExists(source.ref, target.ref)) {
            return;
        }

        options.push_back(ConnectionResolvedOption{
            source.ref,
            target.ref,
            QStringLiteral("%1.%2 -> %3.%4")
                .arg(source.ref.moduleId, source.ref.portId, target.ref.moduleId, target.ref.portId),
            0
        });
    };

    for (const PortSemanticInfo& start : effectiveStartPorts) {
        for (const PortSemanticInfo& end : effectiveEndPorts) {
            if (start.ref.moduleId == end.ref.moduleId) {
                if (rejectionLayer) *rejectionLayer = ConnectionRuleLayer::Structural;
                if (rejectionReason) *rejectionReason = QStringLiteral("self_loop");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Cannot connect a module to itself");
                continue;
            }

            tryAppendOption(start, end, request.start, request.end);
            if (allowReverse) {
                tryAppendOption(end, start, request.end, request.start);
            }
        }
    }
    if (options.isEmpty() && rejectionReason && rejectionReason->isEmpty()) {
        *rejectionReason = QStringLiteral("direction_mismatch");
        if (rejectionMessage) *rejectionMessage = QStringLiteral("No direction-compatible connection option");
    }
    return options;
}

ConnectionCheckResult ConnectionRuleService::check(const ConnectionRequest& request) const {
    if (const std::optional<ConnectionCheckResult> structuralFailure = checkStructuralRules(request)) {
        return *structuralFailure;
    }

    const QVector<PortSemanticInfo> startPorts = resolveEndpointPorts(request.start);
    const QVector<PortSemanticInfo> endPorts = resolveEndpointPorts(request.end);
    if (startPorts.isEmpty() || endPorts.isEmpty()) {
        return reject(ConnectionRuleLayer::Structural,
                      QStringLiteral("missing_port"),
                      QStringLiteral("Connection references a missing port"));
    }

    ConnectionRuleLayer layer = ConnectionRuleLayer::FeaturePlugin;
    QString reason;
    QString message;
    QVector<ConnectionResolvedOption> options = buildOptions(startPorts, endPorts, request, &layer, &reason, &message);
    if (options.isEmpty()) {
        return reject(layer,
                      reason.isEmpty() ? QStringLiteral("no_connection_option") : reason,
                      message.isEmpty() ? QStringLiteral("No legal connection option") : message);
    }

    std::sort(options.begin(), options.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority < rhs.priority || (lhs.priority == rhs.priority && lhs.label < rhs.label);
    });

    ConnectionCheckResult result;
    result.status = options.size() == 1 ? ConnectionCheckStatus::Allowed
                                        : ConnectionCheckStatus::NeedsSelection;
    result.layer = ConnectionRuleLayer::Ipcore;
    result.options = std::move(options);
    return result;
}
