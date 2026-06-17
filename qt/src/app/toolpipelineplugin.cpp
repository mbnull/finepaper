#include "app/toolpipelineplugin.h"

#include "app/appcontext.h"
#include "app/pluginids.h"
#include "app/serviceids.h"
#include "app/serviceregistry.h"
#include "app/toolpipelineservice.h"

#include <stdexcept>

namespace {

bool hasToolPipelineService(AppContext& context) {
    return context.services &&
           context.services->service<ToolPipelineService>(
               app::serviceids::toolPipeline());
}

class ToolPipelinePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return app::pluginids::toolPipeline();
    }

    void activate(AppContext& context) override {
        if (!hasToolPipelineService(context)) {
            throw std::runtime_error(
                "ToolPipelineService is required before activating ToolPipelinePlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createToolPipelinePlugin() {
    return std::make_unique<ToolPipelinePlugin>();
}
