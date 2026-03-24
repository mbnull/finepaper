#include "validator.h"
#include "graph.h"
#include <QSet>

QList<ValidationResult> BasicValidator::validate(const Graph* graph) {
    QList<ValidationResult> results;

    checkInvalidConnections(graph, results);
    checkUnconnectedPorts(graph, results);

    return results;
}

// Check for connections with invalid ports or direction mismatches
void BasicValidator::checkInvalidConnections(const Graph* graph, QList<ValidationResult>& results) {
    for (const auto& conn : graph->connections()) {
        Port* sourcePort = nullptr;
        Port* targetPort = nullptr;

        for (const auto& module : graph->modules()) {
            if (module->id() == conn->source().moduleId) {
                for (const auto& port : module->ports()) {
                    if (port.id() == conn->source().portId) {
                        sourcePort = const_cast<Port*>(&port);
                        break;
                    }
                }
            }
            if (module->id() == conn->target().moduleId) {
                for (const auto& port : module->ports()) {
                    if (port.id() == conn->target().portId) {
                        targetPort = const_cast<Port*>(&port);
                        break;
                    }
                }
            }
        }

        if (!sourcePort || !targetPort) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                "Connection references non-existent port",
                conn->id(),
                "invalid_connection"
            ));
            continue;
        }

        if (sourcePort->direction() != Port::Direction::Output) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                "Connection source must be an output port",
                conn->id(),
                "invalid_connection"
            ));
        }

        if (targetPort->direction() != Port::Direction::Input) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                "Connection target must be an input port",
                conn->id(),
                "invalid_connection"
            ));
        }

        if (sourcePort->type() != targetPort->type()) {
            results.append(ValidationResult(
                ValidationSeverity::Warning,
                QString("Port type mismatch: %1 -> %2").arg(sourcePort->type(), targetPort->type()),
                conn->id(),
                "type_mismatch"
            ));
        }
    }
}

// Check for ports that have no connections (excluding XP routers)
void BasicValidator::checkUnconnectedPorts(const Graph* graph, QList<ValidationResult>& results) {
    QSet<QString> connectedPorts;

    for (const auto& conn : graph->connections()) {
        connectedPorts.insert(conn->source().moduleId + ":" + conn->source().portId);
        connectedPorts.insert(conn->target().moduleId + ":" + conn->target().portId);
    }

    for (const auto& module : graph->modules()) {
        if (module->type() == "XP") {
            continue;
        }

        for (const auto& port : module->ports()) {
            QString portKey = module->id() + ":" + port.id();
            if (!connectedPorts.contains(portKey)) {
                results.append(ValidationResult(
                    ValidationSeverity::Warning,
                    QString("Unconnected %1 port: %2")
                        .arg(port.direction() == Port::Direction::Input ? "input" : "output")
                        .arg(port.name()),
                    module->id(),
                    "unconnected_port"
                ));
            }
        }
    }
}
