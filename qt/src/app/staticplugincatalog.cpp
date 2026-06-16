#include "app/staticplugincatalog.h"

#include "app/plugininteractionregistry.h"
#include "app/pluginhost.h"
#include "app/toolpipelineplugin.h"
#include "ipcore/ipcatalogservice.h"
#include "noc/nocplugin.h"
#include "package/packageplugin.h"
#include "project/projectplugin.h"
#include "workspace/activeworkspacecontroller.h"

#include <QJsonObject>

namespace {

QVector<PluginInteractionDescriptor> topologyInteractions(const PluginInteractionQuery& query) {
    QVector<PluginInteractionDescriptor> interactions;
    if (!query.workspace || !query.entry || !query.workspace->hasActiveIp) {
        return interactions;
    }

    interactions.reserve(query.entry->topologyPresets.size());
    const QString packageId = query.entry->packageId.trimmed().isEmpty()
        ? query.entry->id
        : query.entry->packageId;
    for (const TopologyPresetDescriptor& preset : query.entry->topologyPresets) {
        if (preset.id.trimmed().isEmpty()) {
            continue;
        }
        PluginInteractionDescriptor interaction;
        interaction.id = QStringLiteral("topology:") + preset.id;
        interaction.label = preset.label.trimmed().isEmpty() ? preset.id : preset.label;
        interaction.category = QStringLiteral("Topology");
        interaction.ownerPluginId = QStringLiteral("finepaper.noc-plugin");
        interaction.packageId = packageId;
        interaction.extensionPoint = QStringLiteral("ui.workspaceInteraction");
        interaction.kind = QStringLiteral("topology.preset");
        interaction.descriptor = QJsonObject{
            {QStringLiteral("presetId"), preset.id},
            {QStringLiteral("kind"), preset.kind},
            {QStringLiteral("routerModule"), preset.routerModule},
        };
        interactions.append(interaction);
    }
    return interactions;
}

} // namespace

void registerStaticPlugins(PluginHost& host) {
    host.registerPlugin(createProjectPlugin());
    host.registerPlugin(createPackagePlugin());
    host.registerPlugin(createNoCPlugin());
    host.registerPlugin(createToolPipelinePlugin());
}

bool registerStaticPluginInteractions(PluginInteractionRegistry& interactions) {
    PluginInteractionProviderDescriptor topologyProvider;
    topologyProvider.id = QStringLiteral("finepaper.noc-plugin.topology-provider");
    topologyProvider.ownerPluginId = QStringLiteral("finepaper.noc-plugin");
    topologyProvider.factory = topologyInteractions;
    return interactions.registerProvider(topologyProvider);
}
