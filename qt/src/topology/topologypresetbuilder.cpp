#include "topology/topologypresetbuilder.h"

#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "connection/connectionruleservice.h"
#include "modules/moduleregistry.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace {

TopologyPresetResult failure(const QString& error) {
    TopologyPresetResult result;
    result.error = error;
    return result;
}

QString replaceToken(QString pattern, const QString& token, int value) {
    pattern.replace(QStringLiteral("{") + token + QStringLiteral("}"), QString::number(value));
    return pattern;
}

QString meshNodeId(const QString& pattern, int row, int col) {
    return replaceToken(replaceToken(pattern, QStringLiteral("row"), row), QStringLiteral("col"), col);
}

QString ringNodeId(const QString& pattern, int index) {
    return replaceToken(pattern, QStringLiteral("index"), index);
}

QString scopedGraphId(const QString& instanceId, const QString& logicalId) {
    return instanceId + QLatin1Char('_') + logicalId;
}

QString generatedConnectionId(const QString& sourceModuleId,
                              const QString& sourceInterfaceId,
                              const QString& fallbackSuffix) {
    const QString suffix = sourceInterfaceId.trimmed().isEmpty()
        ? fallbackSuffix
        : sourceInterfaceId.trimmed();
    return sourceModuleId + QLatin1Char('_') + suffix;
}

bool isRouterCapableGraphRole(const QString& graphRole) {
    return graphRole.isEmpty() ||
           graphRole == QStringLiteral("host") ||
           graphRole == QStringLiteral("router");
}

void rollbackCreated(Graph* graph, TopologyPresetResult& result) {
    if (!graph) {
        return;
    }

    for (const QString& connectionId : result.connectionIds) {
        graph->removeConnection(connectionId);
    }
    for (int i = result.moduleIds.size() - 1; i >= 0; --i) {
        graph->removeModule(result.moduleIds.at(i));
    }
    result.connectionIds.clear();
    result.moduleIds.clear();
}

TopologyPresetResult failAndRollback(Graph* graph,
                                     TopologyPresetResult& result,
                                     const QString& error) {
    result.error = error;
    rollbackCreated(graph, result);
    return result;
}

bool connectionIdExists(const Graph* graph, const QString& id) {
    if (!graph) {
        return false;
    }
    for (const auto& connection : graph->connections()) {
        if (connection && connection->id() == id) {
            return true;
        }
    }
    return false;
}

QVector<ConnectionInterfaceRef> graphInterfaceRefs(
    const QVector<ProjectConnectionInterfaceRef>& interfaceRefs) {
    QVector<ConnectionInterfaceRef> refs;
    refs.reserve(interfaceRefs.size());
    for (const ProjectConnectionInterfaceRef& interfaceRef : interfaceRefs) {
        refs.push_back(ConnectionInterfaceRef{interfaceRef.instanceId, interfaceRef.interfaceId});
    }
    return refs;
}

std::unique_ptr<Module> instantiateModule(const ModuleType& type,
                                          const QString& graphId,
                                          const QString& logicalId,
                                          const QString& ipcoreId,
                                          const QString& instanceId,
                                          int row,
                                          int col) {
    auto module = std::make_unique<Module>(graphId, type.name);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    if (module->parameters().contains(QStringLiteral("x"))) {
        module->setParameter(QStringLiteral("x"), col * type.meshSpacingX);
    }
    if (module->parameters().contains(QStringLiteral("y"))) {
        module->setParameter(QStringLiteral("y"), row * type.meshSpacingY);
    }
    if (type.supportsCollapse) {
        module->setParameter(QStringLiteral("collapsed"), false);
    }
    if (module->parameters().contains(QStringLiteral("mesh_col"))) {
        module->setParameter(QStringLiteral("mesh_col"), col);
    }
    if (module->parameters().contains(QStringLiteral("mesh_row"))) {
        module->setParameter(QStringLiteral("mesh_row"), row);
    }
    if (module->parameters().contains(QStringLiteral("display_name"))) {
        module->setParameter(QStringLiteral("display_name"), logicalId);
    }
    if (module->parameters().contains(QStringLiteral("external_id"))) {
        module->setParameter(QStringLiteral("external_id"), logicalId);
    }
    return module;
}

bool addLink(Graph* graph,
             const ConnectionRuleService& ruleService,
             TopologyPresetResult& result,
             const QString& id,
             const QString& sourceModule,
             const QString& sourcePort,
             const QString& targetModule,
             const QString& targetPort) {
    if (connectionIdExists(graph, id)) {
        result.error = QStringLiteral("Connection already exists: %1").arg(id);
        return false;
    }

    const PortRef source{sourceModule, sourcePort};
    const PortRef target{targetModule, targetPort};
    const ConnectionCheckResult check = ruleService.check(
        ConnectionRequest::portToPort(source, target, ConnectionRequestKind::Programmatic));
    if (!check.hasSingleOption()) {
        result.error = QStringLiteral("Generated invalid connection: %1 (%2)")
                           .arg(id, check.reasonCode);
        return false;
    }
    const ConnectionResolvedOption& option = check.options.first();
    if (!graph->isValidConnection(option.source, option.target)) {
        result.error = QStringLiteral("Generated invalid connection: %1").arg(id);
        return false;
    }
    graph->addConnection(std::make_unique<Connection>(
        id,
        option.source,
        option.target,
        option.connectionClassId,
        graphInterfaceRefs(option.normalizedInterfaces),
        option.connectionStatus,
        option.alternatives));
    result.connectionIds.append(id);
    return true;
}

QVector<IpcraftPackageManifest> packageManifestsForPreset(const ModuleRegistry& registry) {
    QVector<IpcraftPackageManifest> manifests = registry.packageManifests();
    const ModuleRegistry& globalRegistry = ModuleRegistry::instance();
    if (&registry != &globalRegistry) {
        const QVector<IpcraftPackageManifest> globalManifests = globalRegistry.packageManifests();
        for (const IpcraftPackageManifest& manifest : globalManifests) {
            const auto existing = std::find_if(
                manifests.cbegin(),
                manifests.cend(),
                [&](const IpcraftPackageManifest& candidate) {
                    return candidate.id == manifest.id;
                });
            if (existing == manifests.cend()) {
                manifests.push_back(manifest);
            }
        }
    }

    return manifests;
}

TopologyPresetResult createMesh(Graph* graph,
                                const ModuleType& routerType,
                                const TopologyPresetRequest& request,
                                QVector<IpcraftPackageManifest> packageManifests) {
    const int rows = request.parameters.value(QStringLiteral("rows"), 2);
    const int cols = request.parameters.value(QStringLiteral("cols"), 2);
    if (rows < 1 || cols < 1) {
        return failure(QStringLiteral("Mesh rows and columns must be positive"));
    }

    TopologyPresetResult result;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QString logicalId = meshNodeId(request.preset.idPattern, row, col);
            const QString graphId = scopedGraphId(request.instanceId, logicalId);
            if (graph->getModule(graphId)) {
                return failAndRollback(graph, result, QStringLiteral("Module already exists: %1").arg(graphId));
            }
            if (!graph->addModule(instantiateModule(routerType,
                                                    graphId,
                                                    logicalId,
                                                    request.ipcoreId,
                                                    request.instanceId,
                                                    row,
                                                    col))) {
                return failAndRollback(graph, result, QStringLiteral("Could not add module: %1").arg(graphId));
            }
            result.moduleIds.append(graphId);
        }
    }

    const QString east = request.preset.ports.value(QStringLiteral("east"));
    const QString west = request.preset.ports.value(QStringLiteral("west"));
    const QString north = request.preset.ports.value(QStringLiteral("north"));
    const QString south = request.preset.ports.value(QStringLiteral("south"));
    const ConnectionRuleService ruleService(graph, {}, std::move(packageManifests));

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QString currentLogical = meshNodeId(request.preset.idPattern, row, col);
            const QString current = scopedGraphId(request.instanceId, currentLogical);
            if (col + 1 < cols) {
                const QString rightLogical = meshNodeId(request.preset.idPattern, row, col + 1);
                const QString right = scopedGraphId(request.instanceId, rightLogical);
                if (!addLink(graph,
                             ruleService,
                             result,
                             generatedConnectionId(current, east, QStringLiteral("east")),
                             current,
                             east,
                             right,
                             west)) {
                    rollbackCreated(graph, result);
                    return result;
                }
            }
            if (row + 1 < rows) {
                const QString belowLogical = meshNodeId(request.preset.idPattern, row + 1, col);
                const QString below = scopedGraphId(request.instanceId, belowLogical);
                if (!addLink(graph,
                             ruleService,
                             result,
                             generatedConnectionId(current, south, QStringLiteral("south")),
                             current,
                             south,
                             below,
                             north)) {
                    rollbackCreated(graph, result);
                    return result;
                }
            }
        }
    }

    result.success = true;
    return result;
}

TopologyPresetResult createRing(Graph* graph,
                                const ModuleType& routerType,
                                const TopologyPresetRequest& request,
                                QVector<IpcraftPackageManifest> packageManifests) {
    const int nodes = request.parameters.value(QStringLiteral("nodes"), 4);
    if (nodes < 2) {
        return failure(QStringLiteral("Ring nodes must be at least 2"));
    }

    TopologyPresetResult result;
    for (int index = 0; index < nodes; ++index) {
        const QString logicalId = ringNodeId(request.preset.idPattern, index);
        const QString graphId = scopedGraphId(request.instanceId, logicalId);
        if (graph->getModule(graphId)) {
            return failAndRollback(graph, result, QStringLiteral("Module already exists: %1").arg(graphId));
        }
        if (!graph->addModule(instantiateModule(routerType,
                                                graphId,
                                                logicalId,
                                                request.ipcoreId,
                                                request.instanceId,
                                                0,
                                                index))) {
            return failAndRollback(graph, result, QStringLiteral("Could not add module: %1").arg(graphId));
        }
        result.moduleIds.append(graphId);
    }

    const QString east = request.preset.ports.value(QStringLiteral("east"));
    const QString west = request.preset.ports.value(QStringLiteral("west"));
    const ConnectionRuleService ruleService(graph, {}, std::move(packageManifests));
    for (int index = 0; index < nodes; ++index) {
        const QString current = scopedGraphId(request.instanceId,
                                              ringNodeId(request.preset.idPattern, index));
        const QString next = scopedGraphId(request.instanceId,
                                           ringNodeId(request.preset.idPattern, (index + 1) % nodes));
        if (!addLink(graph,
                     ruleService,
                     result,
                     generatedConnectionId(current, east, QStringLiteral("next")),
                     current,
                     east,
                     next,
                     west)) {
            rollbackCreated(graph, result);
            return result;
        }
    }

    result.success = true;
    return result;
}

} // namespace

TopologyPresetResult TopologyPresetBuilder::apply(Graph* graph,
                                                  const ModuleRegistry& registry,
                                                  const TopologyPresetRequest& request) {
    if (!graph) {
        return failure(QStringLiteral("Graph is required"));
    }
    if (request.instanceId.trimmed().isEmpty()) {
        return failure(QStringLiteral("Active IP instance is required"));
    }
    const ModuleType* routerType = registry.getType(request.preset.routerModule);
    if (!routerType || routerType->ipcoreId != request.ipcoreId) {
        return failure(QStringLiteral("Router module %1 is not part of active IP %2")
                           .arg(request.preset.routerModule, request.ipcoreId));
    }
    if (!isRouterCapableGraphRole(routerType->graphRole)) {
        return failure(QStringLiteral("Topology module %1 graph role %2 is not host/router-capable")
                           .arg(request.preset.routerModule, routerType->graphRole));
    }
    if (request.preset.kind == QStringLiteral("mesh")) {
        return createMesh(graph,
                          *routerType,
                          request,
                          packageManifestsForPreset(registry));
    }
    if (request.preset.kind == QStringLiteral("ring")) {
        return createRing(graph,
                          *routerType,
                          request,
                          packageManifestsForPreset(registry));
    }
    return failure(QStringLiteral("Unsupported topology preset kind: %1").arg(request.preset.kind));
}
