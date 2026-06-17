#include "project/projectplugin.h"

#include "app/appcontext.h"
#include "app/pluginids.h"
#include "app/serviceids.h"
#include "app/serviceregistry.h"
#include "project/projectservice.h"

#include <stdexcept>

namespace {

bool hasProjectService(AppContext& context) {
    return context.services &&
           context.services->service<ProjectService>(app::serviceids::project());
}

class ProjectPlugin final : public IAppPlugin {
public:
    QString id() const override {
        return app::pluginids::project();
    }

    void activate(AppContext& context) override {
        if (!hasProjectService(context)) {
            throw std::runtime_error("ProjectService is required before activating ProjectPlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createProjectPlugin() {
    return std::make_unique<ProjectPlugin>();
}
