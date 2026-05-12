// IP workspace tools model implementation.
#include "ipcore/iptoolsmodel.h"

#include "ipcore/ipcatalogservice.h"
#include "workspace/activeworkspacecontroller.h"

QVector<IpToolEntry> IpToolsModel::entriesForWorkspace(const ActiveWorkspaceState& state,
                                                       const IpCatalogEntry& entry) const {
    QVector<IpToolEntry> entries;
    if (!state.hasActiveIp) {
        return entries;
    }

    entries.reserve(entry.topologyPresets.size());
    for (const TopologyPresetDescriptor& preset : entry.topologyPresets) {
        entries.push_back(IpToolEntry{
            QStringLiteral("topology:") + preset.id,
            preset.label,
            QStringLiteral("topology"),
        });
    }

    return entries;
}
