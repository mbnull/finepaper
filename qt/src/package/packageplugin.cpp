#include "package/packageplugin.h"

#include "app/appcontext.h"
#include "app/extensionpointregistry.h"
#include "app/serviceregistry.h"
#include "package/packageservice.h"

#include <stdexcept>

namespace {

PackageService* packageService(AppContext& context) {
    if (context.services) {
        if (PackageService* service =
                context.services->service<PackageService>(ServiceKey::fromLiteral("finepaper.package"))) {
            return service;
        }
    }
    return context.packageService;
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
