#include "app/plugininteractionregistry.h"

#include "ipcore/ipcatalogservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QSet>

#include <utility>

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

bool providerValid(const PluginInteractionProviderDescriptor& provider) {
    return canonical(provider.id) &&
           canonical(provider.ownerPluginId) &&
           static_cast<bool>(provider.factory);
}

bool handlerMatchValid(const PluginInteractionHandlerDescriptor& handler) {
    return canonical(handler.interactionIdPrefix) || canonical(handler.interactionKind);
}

bool handlerValid(const PluginInteractionHandlerDescriptor& handler) {
    return canonical(handler.id) &&
           canonical(handler.ownerPluginId) &&
           handlerMatchValid(handler) &&
           static_cast<bool>(handler.handler);
}

bool interactionValid(const PluginInteractionDescriptor& interaction) {
    return canonical(interaction.id);
}

PluginInteractionDescriptor normalizedInteraction(PluginInteractionDescriptor interaction,
                                                  const IpCatalogEntry& entry) {
    if (interaction.label.trimmed().isEmpty()) {
        interaction.label = interaction.id;
    }
    if (interaction.packageId.trimmed().isEmpty()) {
        interaction.packageId = entry.packageId.trimmed().isEmpty() ? entry.id : entry.packageId;
    }
    return interaction;
}

bool handlerMatchesKind(const PluginInteractionHandlerDescriptor& handler,
                        const PluginInteractionDescriptor& interaction) {
    return !handler.interactionKind.trimmed().isEmpty() &&
           handler.interactionKind == interaction.kind;
}

bool handlerMatchesPrefix(const PluginInteractionHandlerDescriptor& handler,
                          const PluginInteractionDescriptor& interaction) {
    return !handler.interactionIdPrefix.trimmed().isEmpty() &&
           interaction.id.startsWith(handler.interactionIdPrefix);
}

} // namespace

bool PluginInteractionRegistry::registerProvider(
    const PluginInteractionProviderDescriptor& provider) {
    if (!providerValid(provider) ||
        hasProviderId(provider.id) ||
        m_providers.size() >= kMaxProviders) {
        return false;
    }
    m_providers.append(provider);
    return true;
}

bool PluginInteractionRegistry::registerHandler(
    const PluginInteractionHandlerDescriptor& handler) {
    if (!handlerValid(handler) ||
        hasHandlerId(handler.id) ||
        m_handlers.size() >= kMaxHandlers) {
        return false;
    }
    m_handlers.append(handler);
    return true;
}

QVector<PluginInteractionProviderDescriptor> PluginInteractionRegistry::providers() const {
    return m_providers;
}

QVector<PluginInteractionHandlerDescriptor> PluginInteractionRegistry::handlers() const {
    return m_handlers;
}

QVector<PluginInteractionDescriptor> PluginInteractionRegistry::interactionsForWorkspace(
    const ActiveWorkspaceState& workspace,
    const IpCatalogEntry& entry,
    const PackageCoverageReport* coverage) const {
    QVector<PluginInteractionDescriptor> result;
    QSet<QString> seenIds;
    const PluginInteractionQuery query{&workspace, &entry, coverage};

    for (const PluginInteractionProviderDescriptor& provider : m_providers) {
        const QVector<PluginInteractionDescriptor> provided = provider.factory(query);
        for (PluginInteractionDescriptor interaction : provided) {
            if (!interactionValid(interaction) ||
                seenIds.contains(interaction.id) ||
                result.size() >= kMaxProjectedInteractions) {
                continue;
            }
            seenIds.insert(interaction.id);
            result.append(normalizedInteraction(std::move(interaction), entry));
        }
    }

    return result;
}

std::optional<PluginInteractionDescriptor> PluginInteractionRegistry::interactionForWorkspace(
    const QString& interactionId,
    const ActiveWorkspaceState& workspace,
    const IpCatalogEntry& entry,
    const PackageCoverageReport* coverage) const {
    if (!canonical(interactionId)) {
        return std::nullopt;
    }

    const QVector<PluginInteractionDescriptor> interactions =
        interactionsForWorkspace(workspace, entry, coverage);
    for (const PluginInteractionDescriptor& interaction : interactions) {
        if (interaction.id == interactionId) {
            return interaction;
        }
    }
    return std::nullopt;
}

PluginInteractionResult PluginInteractionRegistry::dispatch(
    const PluginInteractionDescriptor& interaction,
    const PluginInteractionContext& context) const {
    for (const PluginInteractionHandlerDescriptor& handler : m_handlers) {
        if (!handlerMatchesKind(handler, interaction)) {
            continue;
        }
        PluginInteractionResult result = handler.handler(interaction, context);
        if (!result.handled) {
            result.handled = true;
        }
        return result;
    }

    for (const PluginInteractionHandlerDescriptor& handler : m_handlers) {
        if (!handlerMatchesPrefix(handler, interaction)) {
            continue;
        }
        PluginInteractionResult result = handler.handler(interaction, context);
        if (!result.handled) {
            result.handled = true;
        }
        return result;
    }

    PluginInteractionResult result;
    result.message = QStringLiteral("No interaction handler is registered.");
    return result;
}

bool PluginInteractionRegistry::hasProviderId(const QString& id) const {
    for (const PluginInteractionProviderDescriptor& provider : m_providers) {
        if (provider.id == id) {
            return true;
        }
    }
    return false;
}

bool PluginInteractionRegistry::hasHandlerId(const QString& id) const {
    for (const PluginInteractionHandlerDescriptor& handler : m_handlers) {
        if (handler.id == id) {
            return true;
        }
    }
    return false;
}
