#include "connection/connectionruleservice.h"

#include "common/portlayout.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"

#include <algorithm>
#include <memory>
#include <utility>

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

            if (start.supportsOutput && end.supportsInput &&
                start.busType == end.busType &&
                !connectionExists(m_graph, start.ref, end.ref)) {
                options.push_back(ConnectionResolvedOption{
                    start.ref,
                    end.ref,
                    QStringLiteral("%1.%2 -> %3.%4")
                        .arg(start.ref.moduleId, start.ref.portId, end.ref.moduleId, end.ref.portId),
                    0
                });
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
