// Active workspace controller implementation.
#include "workspace/activeworkspacecontroller.h"

#include "ipcore/ipcatalogservice.h"
#include "project/projectipservice.h"

#include <optional>

namespace {

bool topologyParameterEquals(const TopologyPresetParameterDescriptor& left,
                             const TopologyPresetParameterDescriptor& right) {
    return left.label == right.label
        && left.defaultValue == right.defaultValue
        && left.minimumValue == right.minimumValue
        && left.maximumValue == right.maximumValue;
}

bool topologyParametersEqual(const QHash<QString, TopologyPresetParameterDescriptor>& left,
                             const QHash<QString, TopologyPresetParameterDescriptor>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (auto it = left.cbegin(); it != left.cend(); ++it) {
        const auto rightIt = right.constFind(it.key());
        if (rightIt == right.cend() || !topologyParameterEquals(it.value(), rightIt.value())) {
            return false;
        }
    }
    return true;
}

bool topologyPresetEquals(const TopologyPresetDescriptor& left,
                          const TopologyPresetDescriptor& right) {
    return left.id == right.id
        && left.label == right.label
        && left.kind == right.kind
        && left.routerModule == right.routerModule
        && left.idPattern == right.idPattern
        && left.ports == right.ports
        && topologyParametersEqual(left.parameters, right.parameters);
}

bool topologyPresetsEqual(const QVector<TopologyPresetDescriptor>& left,
                          const QVector<TopologyPresetDescriptor>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (qsizetype index = 0; index < left.size(); ++index) {
        if (!topologyPresetEquals(left.at(index), right.at(index))) {
            return false;
        }
    }
    return true;
}

bool activeWorkspaceStateEquals(const ActiveWorkspaceState& left,
                                const ActiveWorkspaceState& right) {
    return left.hasActiveIp == right.hasActiveIp
        && left.ipcoreId == right.ipcoreId
        && left.instanceId == right.instanceId
        && left.label == right.label
        && left.kind == right.kind
        && left.moduleTypes == right.moduleTypes
        && topologyPresetsEqual(left.topologyPresets, right.topologyPresets);
}

ActiveWorkspaceState stateFromEntry(const ProjectIpInstanceRef& selected,
                                    const IpCatalogEntry& entry) {
    ActiveWorkspaceState state;
    state.hasActiveIp = true;
    state.ipcoreId = selected.ipcoreId;
    state.instanceId = selected.instanceId;
    state.label = entry.name.trimmed().isEmpty() ? entry.id : entry.name;
    state.kind = entry.kind;
    state.moduleTypes = entry.moduleTypes;
    state.topologyPresets = entry.topologyPresets;
    return state;
}

} // namespace

ActiveWorkspaceController::ActiveWorkspaceController(ProjectIpService* projectIpService,
                                                     const IpCatalogService* catalogService,
                                                     QObject* parent)
    : QObject(parent),
      m_projectIpService(projectIpService),
      m_catalogService(catalogService) {
    if (m_projectIpService) {
        connect(m_projectIpService,
                &ProjectIpService::selectedIpInstanceChanged,
                this,
                &ActiveWorkspaceController::recompute);
    }
    recompute();
}

const ActiveWorkspaceState& ActiveWorkspaceController::state() const {
    return m_state;
}

void ActiveWorkspaceController::recompute() {
    ActiveWorkspaceState nextState;
    if (m_projectIpService && m_catalogService) {
        const std::optional<ProjectIpInstanceRef> selected =
            m_projectIpService->selectedIpInstance();
        if (selected.has_value()) {
            const std::optional<IpCatalogEntry> entry =
                m_catalogService->entry(selected->ipcoreId);
            if (entry.has_value()) {
                nextState = stateFromEntry(*selected, *entry);
            }
        }
    }

    if (activeWorkspaceStateEquals(m_state, nextState)) {
        return;
    }

    m_state = nextState;
    emit activeWorkspaceChanged();
}
