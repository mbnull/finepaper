#include "package/packageplugin.h"

#include "app/appcontext.h"
#include "app/extensionpointregistry.h"
#include "app/plugininteractionregistry.h"
#include "app/serviceregistry.h"
#include "package/packagecoverage.h"
#include "package/packageservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QJsonObject>

#include <stdexcept>

namespace {

PackageService* packageService(AppContext& context) {
    if (context.services) {
        if (PackageService* service =
                context.services->service<PackageService>(ServiceKey::fromLiteral("finepaper.package"))) {
            return service;
        }
    }
    return nullptr;
}

QString descriptorLabel(const PackageFeatureCoverageItem& item) {
    const QJsonObject metadata = item.descriptor.value(QStringLiteral("metadata")).toObject();
    const QString metadataLabel = metadata.value(QStringLiteral("label")).toString().trimmed();
    if (!metadataLabel.isEmpty()) {
        return metadataLabel;
    }
    const QString label = item.descriptor.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty()) {
        return label;
    }
    const QString name = item.descriptor.value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    return item.label.trimmed().isEmpty() ? item.id : item.label;
}

QString interactionKindForCoverageId(const QString& id) {
    const qsizetype separator = id.indexOf(QLatin1Char(':'));
    if (separator <= 0) {
        return QStringLiteral("package.feature");
    }
    return QStringLiteral("package.") + id.left(separator);
}

QVector<PluginInteractionDescriptor> packageCoverageInteractions(
    const PluginInteractionQuery& query) {
    QVector<PluginInteractionDescriptor> interactions;
    if (!query.workspace || !query.workspace->hasActiveIp || !query.coverage) {
        return interactions;
    }

    interactions.reserve(query.coverage->items.size());
    for (const PackageFeatureCoverageItem& item : query.coverage->items) {
        if (item.id.trimmed().isEmpty()) {
            continue;
        }
        PluginInteractionDescriptor interaction;
        interaction.id = QStringLiteral("package:") + item.id;
        interaction.label = descriptorLabel(item);
        interaction.category = QStringLiteral("Package");
        interaction.ownerPluginId = QStringLiteral("finepaper.package");
        interaction.packageId = query.coverage->packageId;
        interaction.kind = interactionKindForCoverageId(item.id);
        if (item.id.startsWith(QStringLiteral("capability:"))) {
            interaction.capabilityId = item.id.mid(QStringLiteral("capability:").size());
        }
        interaction.extensionPoint = QStringLiteral("ui.workspaceInteraction");
        interaction.enabled = item.status != PackageFeatureCoverageStatus::Invalid;
        interaction.descriptor = item.descriptor;
        interaction.descriptor.insert(QStringLiteral("coverageId"), item.id);
        interaction.descriptor.insert(QStringLiteral("coverageStatus"),
                                      static_cast<int>(item.status));
        interaction.descriptor.insert(QStringLiteral("coverageStatusLabel"),
                                      packageFeatureCoverageStatusLabel(item.status));
        interaction.descriptor.insert(QStringLiteral("coverageMessage"), item.message);
        interactions.append(interaction);
    }
    return interactions;
}

PluginInteractionResult acceptPackageCoverageInteraction(
    const PluginInteractionDescriptor& interaction,
    const PluginInteractionContext& context) {
    Q_UNUSED(context);

    PluginInteractionResult result;
    result.handled = true;
    result.success = interaction.enabled;
    result.message = interaction.enabled
        ? QStringLiteral("Package interaction accepted.")
        : QStringLiteral("Package interaction is disabled.");
    return result;
}

class PackagePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.package");
    }

    void activate(AppContext& context) override {
        PackageService* service = packageService(context);
        if (!service) {
            throw std::runtime_error("PackageService is required before activating PackagePlugin.");
        }
        service->setCapabilityRegistry(context.capabilities);

        if (context.interactions) {
            PluginInteractionProviderDescriptor provider;
            provider.id = QStringLiteral("finepaper.package.coverage-interactions");
            provider.ownerPluginId = QStringLiteral("finepaper.package");
            provider.factory = packageCoverageInteractions;
            if (!context.interactions->registerProvider(provider)) {
                throw std::runtime_error("Package coverage interaction provider could not be registered.");
            }

            PluginInteractionHandlerDescriptor handler;
            handler.id = QStringLiteral("finepaper.package.coverage-interaction-handler");
            handler.ownerPluginId = QStringLiteral("finepaper.package");
            handler.interactionIdPrefix = QStringLiteral("package:");
            handler.handler = acceptPackageCoverageInteraction;
            if (!context.interactions->registerHandler(handler)) {
                throw std::runtime_error("Package coverage interaction handler could not be registered.");
            }
        }

        ExtensionContribution contribution;
        contribution.id = QStringLiteral("finepaper.package.coverage-inspector");
        contribution.extensionPoint = QStringLiteral("ui.inspectorSection");
        contribution.ownerPluginId = QStringLiteral("finepaper.package");
        contribution.label = QStringLiteral("Package Coverage");
        if (!context.extensionPoints->registerContribution(contribution)) {
            throw std::runtime_error("Package coverage inspector contribution could not be registered.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createPackagePlugin() {
    return std::make_unique<PackagePlugin>();
}
