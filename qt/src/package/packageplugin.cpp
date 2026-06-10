#include "package/packageplugin.h"

#include "app/appcontext.h"

#include <stdexcept>

namespace {

class PackagePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.package");
    }

    void activate(AppContext& context) override {
        if (!context.packageService) {
            throw std::runtime_error("PackageService is required before activating PackagePlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createPackagePlugin() {
    return std::make_unique<PackagePlugin>();
}
