#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <optional>

struct ActiveWorkspaceState;
struct IpCatalogEntry;
struct PackageCoverageReport;

struct PluginInteractionDescriptor {
    QString id;
    QString label;
    QString category;
    QString ownerPluginId;
    QString packageId;
    QString capabilityId;
    QString extensionPoint;
    QString kind;
    bool enabled = true;
    QJsonObject descriptor;
};

struct PluginInteractionQuery {
    const ActiveWorkspaceState* workspace = nullptr;
    const IpCatalogEntry* entry = nullptr;
    const PackageCoverageReport* coverage = nullptr;
};

struct PluginInteractionContext {
    QString ipcoreId;
    QString instanceId;
    QString packageId;
    QJsonObject parameters;
};

struct PluginInteractionResult {
    bool handled = false;
    bool success = false;
    QString message;
};

using PluginInteractionProvider =
    std::function<QVector<PluginInteractionDescriptor>(const PluginInteractionQuery&)>;
using PluginInteractionHandler =
    std::function<PluginInteractionResult(const PluginInteractionDescriptor&,
                                          const PluginInteractionContext&)>;

struct PluginInteractionProviderDescriptor {
    QString id;
    QString ownerPluginId;
    PluginInteractionProvider factory;
};

struct PluginInteractionHandlerDescriptor {
    QString id;
    QString ownerPluginId;
    QString interactionIdPrefix;
    QString interactionKind;
    PluginInteractionHandler handler;
};

class PluginInteractionRegistry {
public:
    static constexpr int kMaxProviders = 1024;
    static constexpr int kMaxHandlers = 1024;
    static constexpr int kMaxProjectedInteractions = 4096;

    bool registerProvider(const PluginInteractionProviderDescriptor& provider);
    bool registerHandler(const PluginInteractionHandlerDescriptor& handler);

    QVector<PluginInteractionProviderDescriptor> providers() const;
    QVector<PluginInteractionHandlerDescriptor> handlers() const;

    QVector<PluginInteractionDescriptor> interactionsForWorkspace(
        const ActiveWorkspaceState& workspace,
        const IpCatalogEntry& entry,
        const PackageCoverageReport* coverage) const;

    std::optional<PluginInteractionDescriptor> interactionForWorkspace(
        const QString& interactionId,
        const ActiveWorkspaceState& workspace,
        const IpCatalogEntry& entry,
        const PackageCoverageReport* coverage) const;

    PluginInteractionResult dispatch(const PluginInteractionDescriptor& interaction,
                                     const PluginInteractionContext& context) const;

private:
    bool hasProviderId(const QString& id) const;
    bool hasHandlerId(const QString& id) const;

    QVector<PluginInteractionProviderDescriptor> m_providers;
    QVector<PluginInteractionHandlerDescriptor> m_handlers;
};
