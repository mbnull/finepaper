// BasicValidator performs framework-level graph checks.
#include "validation/validator.h"
#include "graph/graph.h"
#include "common/portlayout.h"

QList<ValidationResult> BasicValidator::validate(const Graph* graph) {
    QList<ValidationResult> results;

    checkInvalidConnections(graph, results);

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

        if (!PortLayout::supportsOutput(*sourcePort)) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                "Connection source must be an output or inout port",
                conn->id(),
                "invalid_connection"
            ));
        }

        if (!PortLayout::supportsInput(*targetPort)) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                "Connection target must be an input or inout port",
                conn->id(),
                "invalid_connection"
            ));
        }

        if (!PortLayout::sameBusFamily(*sourcePort, *targetPort)) {
            results.append(ValidationResult(
                ValidationSeverity::Warning,
                QString("Port type mismatch: %1 -> %2").arg(sourcePort->type(), targetPort->type()),
                conn->id(),
                "type_mismatch"
            ));
        }
    }
}
