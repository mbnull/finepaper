// RemoveIpInstanceCommand removes a project IP instance with undo/redo support.
#include "legacy/graphcommands/removeipinstancecommand.h"

#include "graph/graph.h"
#include "project/projectstateservice.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <utility>
#include <vector>

namespace {

const Module* moduleById(const Graph* graph, const QString& moduleId) {
    return graph ? graph->getModule(moduleId) : nullptr;
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

bool hasConnectionEndpoints(const Graph* graph, const PortRef& source, const PortRef& target) {
    if (!graph) {
        return false;
    }

    for (const std::unique_ptr<Connection>& connection : graph->connections()) {
        if (!connection) {
            continue;
        }
        if (connection->source() == source && connection->target() == target) {
            return true;
        }
    }
    return false;
}

QString connectionEndpointKey(const PortRef& source, const PortRef& target) {
    return source.moduleId + QStringLiteral("\n") + source.portId + QStringLiteral("\n")
        + target.moduleId + QStringLiteral("\n") + target.portId;
}

bool canRestoreRemovedGraphState(const Graph* graph,
                                 const std::vector<std::unique_ptr<Module>>& removedModules,
                                 const std::vector<std::unique_ptr<Connection>>& removedConnections) {
    if (!graph) {
        return false;
    }

    QHash<QString, const Module*> removedModulesById;
    for (const std::unique_ptr<Module>& module : removedModules) {
        if (!module) {
            continue;
        }
        if (removedModulesById.contains(module->id())) {
            return false;
        }
        if (graph->getModule(module->id())) {
            return false;
        }
        removedModulesById.insert(module->id(), module.get());
    }

    QSet<QString> reservedConnectionEndpoints;
    for (const std::unique_ptr<Connection>& existingConnection : graph->connections()) {
        if (!existingConnection) {
            continue;
        }
        reservedConnectionEndpoints.insert(
            connectionEndpointKey(existingConnection->source(), existingConnection->target()));
    }

    for (const std::unique_ptr<Connection>& connection : removedConnections) {
        if (!connection) {
            continue;
        }

        const PortRef source = connection->source();
        const PortRef target = connection->target();
        if (source.moduleId == target.moduleId) {
            return false;
        }
        const Module* sourceModule =
            moduleById(graph, source.moduleId);
        if (!sourceModule) {
            sourceModule = removedModulesById.value(source.moduleId, nullptr);
        }
        const Module* targetModule =
            moduleById(graph, target.moduleId);
        if (!targetModule) {
            targetModule = removedModulesById.value(target.moduleId, nullptr);
        }

        if (!sourceModule || !targetModule) {
            return false;
        }
        if (!findPort(sourceModule, source.portId) || !findPort(targetModule, target.portId)) {
            return false;
        }
        const QString endpointKey = connectionEndpointKey(source, target);
        if (reservedConnectionEndpoints.contains(endpointKey) || hasConnectionEndpoints(graph, source, target)) {
            return false;
        }
        reservedConnectionEndpoints.insert(endpointKey);
    }

    return true;
}

void restoreConnections(Graph* graph, std::vector<std::unique_ptr<Connection>>& connections) {
    if (!graph) {
        return;
    }

    for (auto& connection : connections) {
        if (connection) {
            graph->insertConnection(std::move(connection));
        }
    }
}

void restoreModulesAndConnections(Graph* graph,
                                  std::vector<std::unique_ptr<Module>>& modules,
                                  std::vector<std::unique_ptr<Connection>>& connections) {
    if (!graph) {
        return;
    }

    for (auto& module : modules) {
        if (module) {
            graph->insertModule(std::move(module));
        }
    }
    restoreConnections(graph, connections);
}

bool sameInstance(const ProjectIpInstanceRef& selection,
                  const QString& ipcoreId,
                  const QString& instanceId) {
    return selection.ipcoreId == ipcoreId && selection.instanceId == instanceId;
}

std::optional<ProjectIpInstanceRef> selectionAfterRemovingRecord(
    const ProjectStateService* stateService,
    int removedIndex,
    const std::optional<ProjectIpInstanceRef>& previousSelection,
    const QString& removedIpcoreId,
    const QString& removedInstanceId) {
    if (!stateService) {
        return std::nullopt;
    }
    if (previousSelection.has_value()
        && !sameInstance(*previousSelection, removedIpcoreId, removedInstanceId)
        && stateService->indexOfIpInstanceRecord(previousSelection->ipcoreId,
                                                previousSelection->instanceId) >= 0) {
        return previousSelection;
    }

    const QVector<ProjectIpInstanceRecord>& records = stateService->ipInstanceRecords();
    if (records.isEmpty()) {
        return std::nullopt;
    }

    const int index = std::clamp(removedIndex, 0, static_cast<int>(records.size()) - 1);
    const ProjectIpInstanceRecord& record = records.at(index);
    return ProjectIpInstanceRef{record.ipcoreId, record.instanceId};
}

} // namespace

RemoveIpInstanceCommand::RemoveIpInstanceCommand(Graph* graph,
                                                 ProjectStateService* stateService,
                                                 ProjectIpService* ipService,
                                                 QString ipcoreId,
                                                 QString instanceId)
    : m_graph(graph),
      m_stateService(stateService),
      m_ipService(ipService),
      m_ipcoreId(std::move(ipcoreId)),
      m_instanceId(std::move(instanceId)) {}

void RemoveIpInstanceCommand::execute() {
    m_executed = false;
    m_undone = false;
    m_recordIndex = -1;
    m_removedRecord.reset();
    m_previousSelection.reset();
    m_removedModules.clear();
    m_removedConnections.clear();

    if (!m_graph || !m_stateService || !m_ipService) {
        return;
    }
    if (m_ipcoreId.trimmed().isEmpty() || m_instanceId.trimmed().isEmpty()) {
        return;
    }

    const std::optional<ProjectIpInstanceRecord> record =
        m_stateService->ipInstanceRecord(m_ipcoreId, m_instanceId);
    if (!record.has_value()) {
        return;
    }

    const int recordIndex = m_stateService->indexOfIpInstanceRecord(m_ipcoreId, m_instanceId);
    if (recordIndex < 0) {
        return;
    }

    const std::optional<ProjectIpInstanceRef> previousSelection = m_ipService->selectedIpInstance();

    std::vector<QString> ownedModuleIds;
    ownedModuleIds.reserve(m_graph->modules().size());
    QSet<QString> ownedModuleIdSet;
    for (const std::unique_ptr<Module>& module : m_graph->modules()) {
        if (!module) {
            continue;
        }
        if (module->ipcoreId() == m_ipcoreId && module->instanceId() == m_instanceId) {
            ownedModuleIds.push_back(module->id());
            ownedModuleIdSet.insert(module->id());
        }
    }

    std::vector<QString> connectionIds;
    connectionIds.reserve(m_graph->connections().size());
    for (const std::unique_ptr<Connection>& connection : m_graph->connections()) {
        if (!connection) {
            continue;
        }
        if (ownedModuleIdSet.contains(connection->source().moduleId)
            || ownedModuleIdSet.contains(connection->target().moduleId)) {
            connectionIds.push_back(connection->id());
        }
    }

    std::vector<std::unique_ptr<Connection>> removedConnections;
    removedConnections.reserve(connectionIds.size());
    for (const QString& connectionId : connectionIds) {
        std::unique_ptr<Connection> connection = m_graph->takeConnection(connectionId);
        if (!connection) {
            restoreConnections(m_graph, removedConnections);
            return;
        }
        removedConnections.push_back(std::move(connection));
    }

    std::vector<std::unique_ptr<Module>> removedModules;
    removedModules.reserve(ownedModuleIds.size());
    for (const QString& moduleId : ownedModuleIds) {
        std::unique_ptr<Module> module = m_graph->takeModule(moduleId);
        if (!module) {
            restoreModulesAndConnections(m_graph, removedModules, removedConnections);
            return;
        }
        removedModules.push_back(std::move(module));
    }

    std::optional<ProjectIpInstanceRecord> removedRecord =
        m_stateService->takeIpInstanceRecord(m_ipcoreId, m_instanceId);
    if (!removedRecord.has_value()) {
        restoreModulesAndConnections(m_graph, removedModules, removedConnections);
        return;
    }

    m_recordIndex = recordIndex;
    m_removedRecord = std::move(removedRecord);
    m_previousSelection = previousSelection;
    m_removedModules = std::move(removedModules);
    m_removedConnections = std::move(removedConnections);
    m_ipService->handleIpInstanceRecordsMutated(
        selectionAfterRemovingRecord(m_stateService,
                                     m_recordIndex,
                                     m_previousSelection,
                                     m_ipcoreId,
                                     m_instanceId));
    m_executed = true;
}

void RemoveIpInstanceCommand::undo() {
    m_undone = false;
    if (!m_graph || !m_stateService || !m_ipService || !m_removedRecord.has_value()) {
        return;
    }
    if (!canRestoreRemovedGraphState(m_graph, m_removedModules, m_removedConnections)) {
        return;
    }
    if (!m_stateService->insertIpInstanceRecord(m_recordIndex, *m_removedRecord)) {
        return;
    }

    QStringList restoredModuleIds;
    for (auto& module : m_removedModules) {
        if (module) {
            const QString moduleId = module->id();
            if (!m_graph->insertModule(module->clone())) {
                for (const QString& restoredModuleId : restoredModuleIds) {
                    m_graph->takeModule(restoredModuleId);
                }
                m_stateService->takeIpInstanceRecord(m_removedRecord->ipcoreId,
                                                     m_removedRecord->instanceId);
                return;
            }
            restoredModuleIds.push_back(moduleId);
        }
    }
    int restoredConnections = 0;
    for (auto& connection : m_removedConnections) {
        if (connection) {
            const qsizetype connectionCountBefore = static_cast<qsizetype>(m_graph->connections().size());
            m_graph->insertConnection(connection->clone());
            const qsizetype connectionCountAfter = static_cast<qsizetype>(m_graph->connections().size());
            if (connectionCountAfter != connectionCountBefore + 1) {
                while (restoredConnections > 0) {
                    const std::unique_ptr<Connection>& restoredConnection =
                        m_removedConnections.at(static_cast<std::size_t>(restoredConnections - 1));
                    if (restoredConnection) {
                        m_graph->takeConnection(restoredConnection->id());
                    }
                    --restoredConnections;
                }
                for (const QString& restoredModuleId : restoredModuleIds) {
                    m_graph->takeModule(restoredModuleId);
                }
                m_stateService->takeIpInstanceRecord(m_removedRecord->ipcoreId,
                                                     m_removedRecord->instanceId);
                return;
            }
            ++restoredConnections;
        }
    }

    m_ipService->handleIpInstanceRecordsMutated(
        m_previousSelection,
        ProjectIpService::SelectionFallbackPolicy::ExactOrClear);
    m_undone = true;
}
