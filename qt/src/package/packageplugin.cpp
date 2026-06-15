#include "package/packageplugin.h"

#include "app/appcontext.h"
#include "app/serviceregistry.h"
#include "package/packageservice.h"

#include <stdexcept>

namespace {

bool hasPackageService(AppContext& context) {
    if (context.services &&
        context.services->service<PackageService>(ServiceKey::fromLiteral("finepaper.package"))) {
        return true;
    }
    return context.packageService != nullptr;
}

class PackagePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.package");
    }

    void activate(AppContext& context) override {
        if (!hasPackageService(context)) {
            throw std::runtime_error("PackageService is required before activating PackagePlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createPackagePlugin() {
    return std::make_unique<PackagePlugin>();
}
