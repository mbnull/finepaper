#include "project/projectplugin.h"

#include "app/appcontext.h"
#include "app/serviceregistry.h"
#include "project/projectservice.h"

#include <stdexcept>

namespace {

bool hasProjectService(AppContext& context) {
    if (context.services &&
        context.services->service<ProjectService>(ServiceKey::fromLiteral("finepaper.project"))) {
        return true;
    }
    return context.projectService != nullptr;
}

class ProjectPlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.project");
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
