// BasicValidator performs built-in graph checks (connection validity and required links).
#include "validation/validator.h"
#include "graph/graph.h"
#include "modules/moduletypemetadata.h"
#include "common/portlayout.h"
#include <QSet>

QList<ValidationResult> BasicValidator::validate(const Graph* graph) {
    QList<ValidationResult> results;

    checkInvalidConnections(graph, results);
    checkIsolatedXps(graph, results);
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

// Router XP nodes may stand alone only when the graph contains that single module.
void BasicValidator::checkIsolatedXps(const Graph* graph, QList<ValidationResult>& results) {
    QList<const Module*> xpModules;
    QSet<QString> connectedModuleIds;

    for (const auto& module : graph->modules()) {
        if (ModuleTypeMetadata::hasEditorLayout(module.get(), u"mesh_router")) {
            xpModules.append(module.get());
        }
    }

    if (xpModules.isEmpty()) {
        return;
    }

    const bool singleStandaloneXpAllowed =
        graph->modules().size() == 1 && xpModules.size() == 1;

    for (const auto& conn : graph->connections()) {
        connectedModuleIds.insert(conn->source().moduleId);
        connectedModuleIds.insert(conn->target().moduleId);
    }

    for (const Module* xp : xpModules) {
        if (connectedModuleIds.contains(xp->id())) {
            continue;
        }

        if (singleStandaloneXpAllowed) {
            continue;
        }

        results.append(ValidationResult(
            ValidationSeverity::Error,
            "Isolated XP must be connected unless it is the only module in the graph",
            xp->id(),
            "isolated_xp"
        ));
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
        if (ModuleTypeMetadata::hasEditorLayout(module.get(), u"mesh_router")) {
            continue;
        }

        for (const auto& port : module->ports()) {
            QString portKey = module->id() + ":" + port.id();
            if (!connectedPorts.contains(portKey)) {
                results.append(ValidationResult(
                    ValidationSeverity::Warning,
                    QString("Unconnected %1 port: %2")
                        .arg(PortLayout::directionLabel(port))
                        .arg(port.name()),
                    module->id(),
                    "unconnected_port"
                ));
            }
        }
    }
}
