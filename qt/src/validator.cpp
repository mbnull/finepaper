#include "validator.h"
#include "graph.h"
#include <QSet>

QList<ValidationResult> BasicValidator::validate(const Graph* graph) {
    QList<ValidationResult> results;

    checkInvalidConnections(graph, results);
    checkUnconnectedPorts(graph, results);

    return results;
}

void BasicValidator::checkInvalidConnections(const Graph* graph, QList<ValidationResult>& results) {
    for (const auto& conn : graph->connections()) {
        Port* sourcePort = nullptr;
        Port* targetPort = nullptr;

        for (const auto& module : graph->modules()) {
            for (const auto& port : module->ports()) {
                if (port.id() == conn->sourcePortId()) {
                    sourcePort = const_cast<Port*>(&port);
                }
                if (port.id() == conn->targetPortId()) {
                    targetPort = const_cast<Port*>(&port);
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

void BasicValidator::checkUnconnectedPorts(const Graph* graph, QList<ValidationResult>& results) {
    QSet<QString> connectedPorts;

    for (const auto& conn : graph->connections()) {
        connectedPorts.insert(conn->sourcePortId());
        connectedPorts.insert(conn->targetPortId());
    }

    for (const auto& module : graph->modules()) {
        for (const auto& port : module->ports()) {
            if (!connectedPorts.contains(port.id())) {
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
