#include "project/projectplugin.h"

#include "app/appcontext.h"

#include <stdexcept>

namespace {

class ProjectPlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.project");
    }

    void activate(AppContext& context) override {
        if (!context.projectService) {
            throw std::runtime_error("ProjectService is required before activating ProjectPlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createProjectPlugin() {
    return std::make_unique<ProjectPlugin>();
}
